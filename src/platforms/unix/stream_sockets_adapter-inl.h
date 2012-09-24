// The Firmament project
// Copyright (c) 2011-2012 Malte Schwarzkopf <malte.schwarzkopf@cl.cam.ac.uk>
//
// UNIX stream sockets-based messaging adapter; inline header for templated
// methods.

#ifndef FIRMAMENT_PLATFORMS_UNIX_MESSAGING_STREAMSOCKETS_INL_H
#define FIRMAMENT_PLATFORMS_UNIX_MESSAGING_STREAMSOCKETS_INL_H

#include "misc/map-util.h"
#include "platforms/unix/stream_sockets_adapter.h"
#include "platforms/unix/stream_sockets_channel-inl.h"

#include <string>
#include <utility>

namespace firmament {
namespace platform_unix {
namespace streamsockets {

template <typename T>
void StreamSocketsAdapter<T>::CloseChannel(
    MessagingChannelInterface<T>* chan) {
  VLOG(1) << "Shutting down channel " << chan;
  chan->Close();
}

template <typename T>
bool StreamSocketsAdapter<T>::EstablishChannel(
    const string& endpoint_uri,
    MessagingChannelInterface<T>* chan) {
  VLOG(1) << "Establishing channel from endpoint " << endpoint_uri
          << ", chan: " << *chan << "!";
  return chan->Establish(endpoint_uri);
}

template <typename T>
StreamSocketsAdapter<T>::~StreamSocketsAdapter() {
  VLOG(2) << "Messaging adapter is being destroyed.";
  StopListen();
}

template <typename T>
void StreamSocketsAdapter<T>::AddChannelForConnection(
    TCPConnection::connection_ptr connection) {
  shared_ptr<StreamSocketsChannel<T> > channel(
          new StreamSocketsChannel<T>(connection));
  const string endpoint_name = connection->RemoteEndpointString();
  VLOG(1) << "Adding back-channel for connection at " << connection
          << ", channel is " << *channel << ", remote endpoint: "
          << endpoint_name;
  pair<const string, shared_ptr<StreamSocketsChannel<T> > > val(
      endpoint_name, channel);
  endpoint_channel_map_.insert(val);
}

template <typename T>
shared_ptr<MessagingChannelInterface<T> >
StreamSocketsAdapter<T>::GetChannelForEndpoint(
    const string& endpoint) {
  CHECK_NE(endpoint, "");
/*  typeof(endpoint_channel_map_.begin()) it =
      endpoint_channel_map_.find(endpoint);*/
  shared_ptr<StreamSocketsChannel<T> >* chan =
      FindOrNull(endpoint_channel_map_, endpoint);
  if (!chan)
    // No channel found
    return shared_ptr<StreamSocketsChannel<T> >();
  // Return channel pointer
  return *chan;
}

template <typename T>
void StreamSocketsAdapter<T>::AwaitNextMessage() {
  // If we have no active channels, we cannot receive any messages, so we return
  // immediately.
  if (endpoint_channel_map_.size() == 0)
    return;
  // Otherwise, let's make sure we have an outstanding async receive request for
  // each fo them.
  bool any_outstanding = false;
  for (typeof(endpoint_channel_map_.begin()) chan_iter =
       endpoint_channel_map_.begin();
       chan_iter != endpoint_channel_map_.end();
       ++chan_iter) {
    shared_ptr<StreamSocketsChannel<T> > chan =
        chan_iter->second;
    if (!channel_recv_envelopes_.count(chan)) {
      // No outstanding receive request for this channel, so create one
      Envelope<T>* envelope = new Envelope<T>();
      channel_recv_envelopes_.insert(
          pair<shared_ptr<StreamSocketsChannel<T> >,
          Envelope<T>*>(chan, envelope));
      VLOG(2) << "MA replenishing envelope for channel " << chan
              << " at " << envelope;
      chan->RecvA(envelope,
                  boost::bind(&StreamSocketsAdapter::HandleAsyncMessageRecv,
                              this->shared_from_this(),
                              boost::asio::placeholders::error,
                              boost::asio::placeholders::bytes_transferred,
                              chan));
      any_outstanding = true;
    }
  }
  if (any_outstanding) {
    // Block until we receive a message somewhere
    VLOG(3) << "About to lock mutex...";
    boost::unique_lock<boost::mutex> lock(message_wait_mutex_);
    VLOG(3) << "Locked!...";
    while (!message_wait_ready_) {
      VLOG(3) << "Waiting for condvar...";
      message_wait_condvar_.wait(lock);
    }
    VLOG(3) << "Message arrived, condvar signalled!";
    message_wait_ready_ = false;
  }
}

template <typename T>
void StreamSocketsAdapter<T>::HandleAsyncMessageRecv(
    const boost::system::error_code& error,
    size_t bytes_transferred,
    shared_ptr<StreamSocketsChannel<T> > chan) {
  if (error) {
    LOG(WARNING) << "Error receiving in MA";
    // TODO(malte): think about clearing up state here. Should we consider the
    // envelope as having been consumed? Currently we do so.
    // XXX(malte): hack, not safe (offset may have changed!)
    endpoint_channel_map_.erase("");
    channel_recv_envelopes_.erase(chan);
    //chan->Close();
    // XXX(malte): Do we need to unlock/signal here?
    message_wait_condvar_.notify_all();
    return;
  }
  CHECK(channel_recv_envelopes_.count(chan));
  Envelope<T>* envelope = channel_recv_envelopes_[chan];
  VLOG(2) << "Received in MA: " << *envelope << " ("
          << bytes_transferred << ")";
  // Invoke message receipt callback, if any registered
  message_recv_handler_(envelope->data());
  // We've finished dealing with this message, so clean up now.
  channel_recv_envelopes_.erase(chan);
  delete envelope;
  {
    boost::lock_guard<boost::mutex> lock(message_wait_mutex_);
    message_wait_ready_ = true;
  }
  // TODO(malte): Not sure if we want notify_all here. So far, we assume that
  // there is only one call to AwaitNextMessage() processing at any time; but
  // this is not enforced. In fact, the below may work in that it gives us what
  // is essentially broadcast semantics if multiple threads are waiting.
  message_wait_condvar_.notify_all();
}

template <typename T>
void StreamSocketsAdapter<T>::Listen(const string& endpoint_uri) {
  // no-op if we are already listening
  /*if (ListenReady())
    return;*/
  CHECK(!ListenReady());
  CHECK_EQ(endpoint_channel_map_.size(), 0);
  CHECK_EQ(channel_recv_envelopes_.size(), 0);
  message_wait_mutex_.lock();
  message_wait_ready_ = false;
  message_wait_mutex_.unlock();
  // Parse endpoint URI into hostname and port
  string hostname = URITools::GetHostnameFromURI(endpoint_uri);
  string port = URITools::GetPortFromURI(endpoint_uri);

  VLOG(1) << "Creating an async TCP server on port " << port
          << " on endpoint " << hostname << "(" << endpoint_uri << ")";
  tcp_server_.reset(new AsyncTCPServer(
      hostname, port, boost::bind(
          &StreamSocketsAdapter::AddChannelForConnection,
          this->shared_from_this(),
          _1)));
  tcp_server_thread_.reset(
      new boost::thread(boost::bind(&AsyncTCPServer::Run, tcp_server_)));
  VLOG(1) << "AsyncTCPServer's main thread running as "
          << tcp_server_thread_->get_id();
}

template <typename T>
bool StreamSocketsAdapter<T>::ListenReady() {
  if (tcp_server_)
    return tcp_server_->listening();
  else
    return false;
}

template <typename T>
void StreamSocketsAdapter<T>::RegisterAsyncMessageReceiptCallback(
    typename AsyncMessageRecvHandler<T>::type callback) {
  message_recv_handler_ = callback;
}

template <typename T>
void StreamSocketsAdapter<T>::RegisterAsyncErrorPathCallback(
    typename AsyncErrorPathHandler<T>::type callback) {
  error_path_handler_ = callback;
}

template <typename T>
bool StreamSocketsAdapter<T>::SendMessageToEndpoint(
    const string& endpoint_uri, T& message) {
  shared_ptr<StreamSocketsChannel<T> >* chan =
      FindOrNull(endpoint_channel_map_, endpoint_uri);
  if (!chan) {
    LOG(ERROR) << "Failed to find channel for endpoint " << endpoint_uri;
    return false;
  }
  // N.B.: Synchronous send here means that it's okay to stack-allocate the
  // Envelope; if we ever switch to async or provide such a facility, this needs
  // to be dynamically allocated.
  Envelope<T> envelope(&message);
  return (*chan)->SendS(envelope);
}

template <typename T>
void StreamSocketsAdapter<T>::StopListen() {
  if (tcp_server_) {
    for (typeof(endpoint_channel_map_.begin()) chan_iter =
         endpoint_channel_map_.begin();
         chan_iter != endpoint_channel_map_.end();
         ++chan_iter) {
      VLOG(2) << "Closing associated channel at " << chan_iter->second;
      chan_iter->second->Close();
    }
    VLOG(2) << "Stopping async TCP server at " << tcp_server_
            << "...";
    tcp_server_->Stop();
    tcp_server_thread_->join();
    VLOG(2) << "TCP server thread joined.";
  }
  // XXX(malte): We would prefer if channels cleared up after themselves, but
  // for the moment, this is a sledgehammer approach.
  endpoint_channel_map_.clear();
  channel_recv_envelopes_.clear();
}

template <class T>
ostream& StreamSocketsAdapter<T>::ToString(ostream* stream) const {
  return *stream << "(MessagingAdapter,type=StreamSockets,at=" << this << ")";
}


}  // namespace streamsockets
}  // namespace platform_unix
}  // namespace firmament

#endif  // FIRMAMENT_PLATFORMS_UNIX_MESSAGING_STREAMSOCKETS_INL_H