/*  Sirikata
 *  SSTImpl.hpp
 *
 *  Copyright (c) 2009, Tahir Azim.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef SST_IMPL_HPP
#define SST_IMPL_HPP

#include <sirikata/core/network/SSTDecls.hpp>

#include <sirikata/core/service/Service.hpp>
#include <sirikata/core/util/Timer.hpp>
#include <sirikata/core/service/Context.hpp>
#include <sirikata/core/network/IOTimer.hpp>

#include <sirikata/core/network/Message.hpp>
#include <sirikata/core/network/ObjectMessage.hpp>
#include "Protocol_SSTHeader.pbj.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp> //htons, ntohs

#include <sirikata/core/options/CommonOptions.hpp>

#define SST_LOG(lvl,msg) SILOG(sst,lvl,msg);

namespace Sirikata {
namespace SST {

template <typename EndObjectType>
class EndPoint {
public:
  EndObjectType endPoint;
  ObjectMessagePort port;

  EndPoint() {
  }

  EndPoint(EndObjectType endPoint, ObjectMessagePort port) {
    this->endPoint = endPoint;
    this->port = port;
  }

  bool operator< (const EndPoint &ep) const{
    if (endPoint != ep.endPoint) {
      return endPoint < ep.endPoint;
    }

    return this->port < ep.port ;
  }

    bool operator==(const EndPoint& ep) const {
        return (
            this->port == ep.port &&
            this->endPoint == ep.endPoint);
    }
    std::size_t hash() const {
        size_t seed = 0;
        boost::hash_combine(seed, typename EndObjectType::Hasher()(endPoint));
        boost::hash_combine(seed, port);
        return seed;
    }

    class Hasher{
    public:
        size_t operator() (const EndPoint& ep) const {
            return ep.hash();
        }
    };

    std::string toString() const {
        return endPoint.toString() + boost::lexical_cast<std::string>(port);
    }
};

class Mutex {
public:

  Mutex() {

  }

  Mutex(const Mutex& mutex) {  }

  boost::mutex& getMutex() {
    return mMutex;
  }

private:
  boost::mutex mMutex;

};

template <typename EndPointType>
class CallbackTypes {
public:
    typedef std::tr1::function< void(int, std::tr1::shared_ptr< Connection<EndPointType> > ) > ConnectionReturnCallbackFunction;
    typedef std::tr1::function< void(int, std::tr1::shared_ptr< Stream<EndPointType> >) >  StreamReturnCallbackFunction;

    typedef std::tr1::function< void (int, void*) >  DatagramSendDoneCallback;
    typedef std::tr1::function<void (uint8*, int) >  ReadDatagramCallback;
    typedef std::tr1::function<void (uint8*, int) > ReadCallback;
};

typedef UUID USID;

typedef uint32 LSID;

template <class EndPointType>
class ConnectionVariables {
public:

    typedef std::tr1::shared_ptr<BaseDatagramLayer<EndPointType> > BaseDatagramLayerPtr;
    typedef CallbackTypes<EndPointType> CBTypes;
    typedef typename CBTypes::ConnectionReturnCallbackFunction ConnectionReturnCallbackFunction;
    typedef typename CBTypes::StreamReturnCallbackFunction StreamReturnCallbackFunction;

  /* Returns 0 if no channel is available. Otherwise returns the lowest
     available channel. */
    uint32 getAvailableChannel(EndPointType& endPointType) {
      BaseDatagramLayerPtr datagramLayer = getDatagramLayer(endPointType);
      assert (datagramLayer != BaseDatagramLayerPtr());

      return datagramLayer->getUnusedPort(endPointType);
    }

    void releaseChannel(EndPointType& ept, uint32 channel) {

      BaseDatagramLayerPtr datagramLayer = getDatagramLayer(ept);
      if (datagramLayer != BaseDatagramLayerPtr()) {

        EndPoint<EndPointType> ep(ept, channel);

        datagramLayer->unlisten(ep);
      }
    }

    BaseDatagramLayerPtr getDatagramLayer(EndPointType& endPoint)
    {
        if (sDatagramLayerMap.find(endPoint) != sDatagramLayerMap.end()) {
            return sDatagramLayerMap[endPoint];
        }

        return BaseDatagramLayerPtr();
    }

    void addDatagramLayer(EndPointType& endPoint, BaseDatagramLayerPtr datagramLayer)
    {
        sDatagramLayerMap[endPoint] = datagramLayer;
    }

    void removeDatagramLayer(EndPointType& endPoint, bool warn = false)
    {
        typename std::tr1::unordered_map<EndPointType, BaseDatagramLayerPtr, typename EndPointType::Hasher >::iterator wherei = sDatagramLayerMap.find(endPoint);
        if (wherei != sDatagramLayerMap.end()) {
            sDatagramLayerMap.erase(wherei);
        } else if (warn) {
            SILOG(sst,error,"FATAL: Invalidating BaseDatagramLayer that's invalid");
        }
    }

private:
    std::tr1::unordered_map<EndPointType, BaseDatagramLayerPtr, typename EndPointType::Hasher > sDatagramLayerMap;

public:
    typedef std::tr1::unordered_map<EndPoint<EndPointType>, StreamReturnCallbackFunction, typename EndPoint<EndPointType>::Hasher> StreamReturnCallbackMap;
    StreamReturnCallbackMap mStreamReturnCallbackMap;

    typedef std::tr1::unordered_map<EndPoint<EndPointType>, std::tr1::shared_ptr<Connection<EndPointType> >, typename EndPoint<EndPointType>::Hasher >  ConnectionMap;
    ConnectionMap sConnectionMap;

    typedef std::tr1::unordered_map<EndPoint<EndPointType>, ConnectionReturnCallbackFunction, typename EndPoint<EndPointType>::Hasher>  ConnectionReturnCallbackMap;
    ConnectionReturnCallbackMap sConnectionReturnCallbackMap;

    StreamReturnCallbackMap  sListeningConnectionsCallbackMap;
    Mutex sStaticMembersLock;

};

// This is just a template definition. The real implementation of BaseDatagramLayer
// lies in libcore/include/sirikata/core/odp/SST.hpp and
// libcore/include/sirikata/core/ohdp/SST.hpp.
template <typename EndPointType>
class SIRIKATA_EXPORT BaseDatagramLayer
{
    // This class connects SST to the underlying datagram protocol. This isn't
    // an implementation -- the implementation will vary significantly for each
    // underlying datagram protocol -- but it does specify the interface. We
    // keep all types private in this version so it is obvious when you are
    // trying to incorrectly use this implementation instead of a real one.
  private:
    typedef std::tr1::shared_ptr<BaseDatagramLayer<EndPointType> > Ptr;
    typedef Ptr BaseDatagramLayerPtr;

    typedef std::tr1::function<void(void*, int)> DataCallback;

    /** Create a datagram layer. Required parameters are the
     *  ConnectionVariables, Endpoint, and Context. Additional variables are
     *  permitted (this is called via a templated function in
     *  ConnectionManager).
     *
     *  Should insert into ConnectionVariable's datagram layer map; should also
     *  reuse existing datagram layers.
     */
    static BaseDatagramLayerPtr createDatagramLayer(
        ConnectionVariables<EndPointType>* sstConnVars,
        EndPointType endPoint,
        const Context* ctx,
        void* extra)
    {
        return BaseDatagramLayerPtr();
    }

    /** Get the datagram layer for the given endpoint, if it exists. */
    static BaseDatagramLayerPtr getDatagramLayer(ConnectionVariables<EndPointType>* sstConnVars,
                                                 EndPointType endPoint)
    {
        return BaseDatagramLayerPtr();
    }

    /** Get the Context for this datagram layer. */
    const Context* context() {
        return NULL;
    }

    /** Get a port that isn't currently in use. */
    uint32 getUnusedPort(const EndPointType& ep) {
      return 0;
    }


    /** Stop listening to the specified endpoint and also remove from the
     *  ConnectionVariables datagram layer map.
     */
    static void stopListening(ConnectionVariables<EndPointType>* sstConnVars, EndPoint<EndPointType>& listeningEndPoint) {
    }

    /** Listen to the specified endpoint and invoke the given callback when data
     *  arrives.
     */
    void listenOn(EndPoint<EndPointType>& listeningEndPoint, DataCallback cb) {
    }

    /** Listen to the specified endpoint, invoking
     *  Connection::handleReceive() when data arrives.
     */
    void listenOn(EndPoint<EndPointType>& listeningEndPoint) {
    }

    /** Send the given data from the given source port (possibly not allocated
     *  yet) to the given destination. This is the core function for outbound
     *  communication.
     */
    void send(EndPoint<EndPointType>* src, EndPoint<EndPointType>* dest, void* data, int len) {
    }

    /** Stop listening on the given endpoint. You can fully deallocate the
     *  underlying resources for the endpoint.
     */
    void unlisten(EndPoint<EndPointType>& ep) {
    }

    /** Mark this BaseDatagramLayer as invalid, ensuring that no more writes to
     *  the underlying datagram protocol will occur. Also remove this
     *  BaseDatagramLayer from the ConnectionVariables.
     */
    void invalidate() {
    }
};

#define SST_IMPL_SUCCESS 0
#define SST_IMPL_FAILURE -1

class ChannelSegment {
public:

  uint8* mBuffer;
  uint16 mBufferLength;
  uint64 mChannelSequenceNumber;
  uint64 mAckSequenceNumber;

  Time mTransmitTime;
  Time mAckTime;

  ChannelSegment( const void* data, int len, uint64 channelSeqNum, uint64 ackSequenceNum) :
                                               mBufferLength(len),
					      mChannelSequenceNumber(channelSeqNum),
					      mAckSequenceNumber(ackSequenceNum),
					      mTransmitTime(Time::null()), mAckTime(Time::null())
  {
    mBuffer = new uint8[len];
    memcpy( mBuffer, (const uint8*) data, len);
  }

  ~ChannelSegment() {
    delete [] mBuffer;
  }

  void setAckTime(Time& ackTime) {
    mAckTime = ackTime;
  }

};

template <class EndPointType>
class SIRIKATA_EXPORT Connection {
  public:
    typedef std::tr1::shared_ptr<Connection> Ptr;
    typedef Ptr ConnectionPtr;

private:
    typedef BaseDatagramLayer<EndPointType> BaseDatagramLayerType;
    typedef std::tr1::shared_ptr<BaseDatagramLayerType> BaseDatagramLayerPtr;

    typedef CallbackTypes<EndPointType> CBTypes;
    typedef typename CBTypes::ConnectionReturnCallbackFunction ConnectionReturnCallbackFunction;
    typedef typename CBTypes::StreamReturnCallbackFunction StreamReturnCallbackFunction;
    typedef typename CBTypes::DatagramSendDoneCallback DatagramSendDoneCallback;
    typedef typename CBTypes::ReadDatagramCallback ReadDatagramCallback;

  friend class Stream<EndPointType>;
  friend class ConnectionManager<EndPointType>;
  friend class BaseDatagramLayer<EndPointType>;

  typedef std::tr1::unordered_map<EndPoint<EndPointType>, std::tr1::shared_ptr<Connection>, typename EndPoint<EndPointType>::Hasher >  ConnectionMap;
  typedef std::tr1::unordered_map<EndPoint<EndPointType>, ConnectionReturnCallbackFunction, typename EndPoint<EndPointType>::Hasher>  ConnectionReturnCallbackMap;
  typedef std::tr1::unordered_map<EndPoint<EndPointType>, StreamReturnCallbackFunction, typename EndPoint<EndPointType>::Hasher> StreamReturnCallbackMap;

  EndPoint<EndPointType> mLocalEndPoint;
  EndPoint<EndPointType> mRemoteEndPoint;

  ConnectionVariables<EndPointType>* mSSTConnVars;
  BaseDatagramLayerPtr mDatagramLayer;

  int mState;
  uint32 mRemoteChannelID;
  uint32 mLocalChannelID;

  uint64 mTransmitSequenceNumber;
  uint64 mLastReceivedSequenceNumber;   //the last transmit sequence number received from the other side

  typedef std::map<LSID, std::tr1::shared_ptr< Stream<EndPointType> > > LSIDStreamMap;
  std::map<LSID, std::tr1::shared_ptr< Stream<EndPointType> > > mOutgoingSubstreamMap;
  std::map<LSID, std::tr1::shared_ptr< Stream<EndPointType> > > mIncomingSubstreamMap;

  std::map<uint32, StreamReturnCallbackFunction> mListeningStreamsCallbackMap;
  std::map<uint32, std::vector<ReadDatagramCallback> > mReadDatagramCallbacks;
  typedef std::vector<std::string> PartialPayloadList;
  typedef std::map<LSID, PartialPayloadList> PartialPayloadMap;
  PartialPayloadMap mPartialReadDatagrams;

  uint32 mNumStreams;

  std::deque< std::tr1::shared_ptr<ChannelSegment> > mQueuedSegments;
  std::deque< std::tr1::shared_ptr<ChannelSegment> > mOutstandingSegments;
  boost::mutex mOutstandingSegmentsMutex;

  uint16 mCwnd;
  int64 mRTOMicroseconds; // RTO in microseconds
  bool mFirstRTO;

  boost::mutex mQueueMutex;

  uint16 MAX_DATAGRAM_SIZE;
  uint16 MAX_PAYLOAD_SIZE;
  uint32 MAX_QUEUED_SEGMENTS;
  float  CC_ALPHA;
  Time mLastTransmitTime;

  std::tr1::weak_ptr<Connection<EndPointType> > mWeakThis;

  uint16 mNumInitialRetransmissionAttempts;

  google::protobuf::LogSilencer logSilencer;

  bool mInSendingMode;

  // We check periodically if all streams have been removed and clean
  // up the connection if they have.
  Network::IOTimerPtr mCheckAliveTimer;

  // We can schedule servicing from multiple threads, so we need to
  // lock protect this data.
  boost::mutex mSchedulingMutex;
  // One timer to track servicing. Only one servicing should be scheduled at any
  // time. Scheduling servicing only does something if nothing is scheduled yet
  // or if the time it's scheduled for is too late, in which case we update the
  // timer.
  Network::IOTimerPtr mServiceTimer;
  // Sometimes we do servicing directly, i.e. just a post. We need to
  // track this to make sure we don't double-schedule because the
  // timer expiry won't be meaningful in that case
  bool mIsAsyncServicing;
  // We need to keep a strong reference to ourselves while we're waiting for
  // servicing. We'll also use this as an indicator of whether servicing is
  // currently scheduled.
  ConnectionPtr mServiceStrongRef;
  // Schedules servicing to occur after the given amount of time.
  void scheduleConnectionService() {
      scheduleConnectionService(Duration::zero());
  }
  void scheduleConnectionService(const Duration& after) {
      boost::mutex::scoped_lock lock(mSchedulingMutex);

      bool needs_scheduling = false;
      if (!mServiceStrongRef) {
          needs_scheduling = true;
      }
      else if(!mIsAsyncServicing && mServiceTimer->expiresFromNow() > after) {
          needs_scheduling = true;
          // No need to check success because we're using a strand and we can
          // only get here if timer->expiresFromNow() is positive.
          mServiceTimer->cancel();
      }

      if (needs_scheduling) {
          mServiceStrongRef = mWeakThis.lock();
          assert(mServiceStrongRef);
          if (after == Duration::zero()) {
              mIsAsyncServicing = true;
              getContext()->mainStrand->post(
                  std::tr1::bind(&Connection<EndPointType>::serviceConnectionNoReturn, this),
                  "Connection<EndPointType>::serviceConnectionNoReturn"
              );
          }
          else {
              mServiceTimer->wait(after);
          }
      }
  }
  // Called when servicing is starting to clear out state and allow
  // the next scheduling. Returns the
  ConnectionPtr startingService() {
      // Need to do some basic bookkeeping that makes sure that a) we
      // have a proper shared_ptr to ourselves, b) we'll hold onto it
      // for the duration of this call and c) we clear out our
      // scheduled servicing time so calls to schedule servicing will
      // work
      boost::mutex::scoped_lock lock(mSchedulingMutex);
      ConnectionPtr conn = mServiceStrongRef;
      assert(conn);
      // Just clearing the strong ref is enough to let someone else schedule
      // servicing.
      mServiceStrongRef.reset();
      // If this was a plain old async call, make sure we unmark it
      mIsAsyncServicing = false;
      return conn;
  }
private:

  Connection(ConnectionVariables<EndPointType>* sstConnVars,
             EndPoint<EndPointType> localEndPoint,
             EndPoint<EndPointType> remoteEndPoint)
    : mLocalEndPoint(localEndPoint), mRemoteEndPoint(remoteEndPoint),
      mSSTConnVars(sstConnVars),
      mDatagramLayer(sstConnVars->getDatagramLayer(localEndPoint.endPoint)),
      mState(CONNECTION_DISCONNECTED),
      mRemoteChannelID(0), mLocalChannelID(1), mTransmitSequenceNumber(1),
      mLastReceivedSequenceNumber(1),
      mNumStreams(0), mCwnd(1), mRTOMicroseconds(2000000),
      mFirstRTO(true),  MAX_DATAGRAM_SIZE(1000), MAX_PAYLOAD_SIZE(1300),
      MAX_QUEUED_SEGMENTS(3000),
      CC_ALPHA(0.8), mLastTransmitTime(Time::null()),
      mNumInitialRetransmissionAttempts(0),
      mInSendingMode(true),
      mCheckAliveTimer(
          Network::IOTimer::create(
              getContext()->mainStrand
              // Don't set callback yet, we need the shared_ptr to ourselves
          )
      ),
      mServiceTimer(
          Network::IOTimer::create(
              getContext()->mainStrand,
              std::tr1::bind(&Connection<EndPointType>::serviceConnectionNoReturn, this)
          )
      ),
      mIsAsyncServicing(false),
      mServiceStrongRef() // Should start NULL
  {
      mDatagramLayer->listenOn(
          localEndPoint,
          std::tr1::bind(
              &Connection::receiveMessageRaw, this,
              std::tr1::placeholders::_1,
              std::tr1::placeholders::_2
          )
      );

  }

  void checkIfAlive(std::tr1::shared_ptr<Connection<EndPointType> > conn) {
    if (mOutgoingSubstreamMap.size() == 0 && mIncomingSubstreamMap.size() == 0) {
      close(true);
      return;
    }

    mCheckAliveTimer->wait(Duration::seconds(300));
  }

  void sendSSTChannelPacket(Sirikata::Protocol::SST::SSTChannelHeader& sstMsg) {
    if (mState == CONNECTION_DISCONNECTED) return;

    std::string buffer = serializePBJMessage(sstMsg);
    mDatagramLayer->send(&mLocalEndPoint, &mRemoteEndPoint, (void*) buffer.data(),
				       buffer.size());
  }

  const Context* getContext() {
    return mDatagramLayer->context();
  }

  void serviceConnectionNoReturn() {
      serviceConnection();
  }

  bool serviceConnection() {
      std::tr1::shared_ptr<Connection<EndPointType> > conn = startingService();

    const Time curTime = Timer::now();

    boost::mutex::scoped_lock lock(mOutstandingSegmentsMutex);


    // Special case: if we've gotten back into serviceConnection while
    // we're waiting for the connection to get setup, we can just
    // clear out the outstanding connection packet. Normally
    // outstanding packets would get cleared by not being in sending
    // mode, but we never change out of sending mode during connection
    // setup.
    if (mState == CONNECTION_PENDING_CONNECT) {
      mOutstandingSegments.clear();
    }

    // should start from ssthresh, the slow start lower threshold, but starting
    // from 1 for now. Still need to implement slow start.
    if (mState == CONNECTION_DISCONNECTED) {
      std::tr1::shared_ptr<Connection<EndPointType> > thus (mWeakThis.lock());
      if (thus) {
        cleanup(thus);
      }else {
        SILOG(sst,error,"FATAL: disconnected lost weak pointer for Connection<EndPointType> too early to call cleanup on it");
      }
      return false;
    }
    else if (mState == CONNECTION_PENDING_DISCONNECT) {
      boost::mutex::scoped_lock lock(mQueueMutex);

      if (mQueuedSegments.empty()) {
        mState = CONNECTION_DISCONNECTED;
        std::tr1::shared_ptr<Connection<EndPointType> > thus (mWeakThis.lock());
        if (thus) {
          cleanup(thus);
        }else {
            SILOG(sst,error,"FATAL: pending disconnection lost weak pointer for Connection<EndPointType> too early to call cleanup on it");
        }
        return false;
      }
    }

    // For the connection, we are in one of two modes: sending or
    // waiting for acks. Sending mode essentially just means we have
    // some packets to send and we've got room in the congestion
    // window, so we're going to be able to push out at least one more
    // packet. Otherwise, we're just waiting for a timeout on the
    // packets to guess that they have been lost, causing the window
    // size to adjust (or nothing is going on with the connection).
    if (mInSendingMode) {
      boost::mutex::scoped_lock lock(mQueueMutex);

      // NOTE: For our current approach, we should never service in
      // sending mode unless we're going to be able to send some
      // data. The correctness of the servicing depends on this since
      // you need to pass through the loop below at least once to
      // adjust some properties (e.g. sending mode).
      assert( !mQueuedSegments.empty() && mOutstandingSegments.size() <= mCwnd);

      for (int i = 0; (!mQueuedSegments.empty()) && mOutstandingSegments.size() <= mCwnd; i++) {
	  std::tr1::shared_ptr<ChannelSegment> segment = mQueuedSegments.front();

	  Sirikata::Protocol::SST::SSTChannelHeader sstMsg;
	  sstMsg.set_channel_id( mRemoteChannelID );
	  sstMsg.set_transmit_sequence_number(segment->mChannelSequenceNumber);
	  sstMsg.set_ack_count(1);
	  sstMsg.set_ack_sequence_number(segment->mAckSequenceNumber);

	  sstMsg.set_payload(segment->mBuffer, segment->mBufferLength);

          /*printf("%s sending packet from data sending loop to %s \n",
                   mLocalEndPoint.endPoint.toString().c_str()
                   , mRemoteEndPoint.endPoint.toString().c_str());*/

	  sendSSTChannelPacket(sstMsg);

	  segment->mTransmitTime = curTime;
	  mOutstandingSegments.push_back(segment);

	  mLastTransmitTime = curTime;

          // If we're setting up the connection, we hold ourselves in
          // sending mode and keep the initial connection packet in
          // the queue so it will get retransmitted if we don't hear
          // back from the other side. Otherwise, we're going to have
          // either filled up the congestion window or run out of
          // packets to send by the time we exit.
          if (mState != CONNECTION_PENDING_CONNECT || mNumInitialRetransmissionAttempts > 5) {
            mInSendingMode = false;
            mQueuedSegments.pop_front();
          }
          // Stop sending packets after the first one if we're setting
          // up the connection since we'll just keep sending the first
          // (unpoppped) packet over and over until the window is
          // filled otherwise.
          if (mState == CONNECTION_PENDING_CONNECT) {
              mNumInitialRetransmissionAttempts++;
              break;
          }
      }

      // After sending, we need to decide when to schedule servicing
      // next. During normal operation, we can end up in two states
      // where we would enter serviceConnection in sending mode and
      // exit here: either we sent all our queued packets and have
      // space in the congestion window or we had to stop because the
      // window was full. In both cases, we need to set a timeout
      // after which we assume packets were dropped. If we get an ack
      // in the meantime, it puts us back in sending mode and
      // schedules servicing immediately, effectively resetting that
      // timer.
      //
      // Even when starting up, we're essentially in the same state --
      // we need to use our current backoff and test for drops. In
      // this case we'll hold ourselves in sending mode (see in the
      // loop above), keeping us from adjusting the congestion window,
      // but we still use the timeout to detect the drop, in that case
      // forcing a retransmit of the connect packet.
      //
      // So essentially, no matter what, if we came through servicing
      // in sending mode, we just use a timeout-drop-detector and
      // other events will trigger us to service earlier if necessary.

      // During startup, RTO hasn't been estimated so we need to have
      // a backoff mechanism to allow for RTTs longer than our initial
      // guess.
      if (mState == CONNECTION_PENDING_CONNECT) {
          // Use numattempts - 1 because we've already incremented
          // here. This way we start out with a factor of 2^0 = 1
          // instead of a factor of 2^1 = 2.
          scheduleConnectionService(Duration::microseconds(mRTOMicroseconds*pow(2.0,(mNumInitialRetransmissionAttempts-1))));
      }
      else {
          // Otherwise, just wait the expected RTT time, plus more to
          // account for jitter.
          scheduleConnectionService(Duration::microseconds(mRTOMicroseconds*2));
      }
    }
    else {
        // If we are not in sending mode, then we had a timeout after
        // previous sends.

        // In the case of enough failures during connect, we just have
        // to give up.
        if (mState == CONNECTION_PENDING_CONNECT) {
            std::tr1::shared_ptr<Connection<EndPointType> > thus (mWeakThis.lock());
            if (thus) {
                cleanup(thus);
            } else {
                SILOG(sst,error,"FATAL: pending connection lost weak pointer for Connection<EndPointType> too early to call cleanup on it");
            }

            return false; //the connection was unable to contact the other endpoint.
        }

        // Otherwise, adjust the congestion window if we have
        // oustanding packets left.
        if (mOutstandingSegments.size() > 0) {
            mCwnd /= 2;
            if (mCwnd < 1)
                mCwnd = 1;

            mOutstandingSegments.clear();
        }

        // And if we have anything to send, put ourselves back into
        // sending mode and schedule servicing. Outstanding segments
        // should be empty, so we're guaranteed to send at least one
        // packet.
        if (!mQueuedSegments.empty()) {
            mInSendingMode = true;
            scheduleConnectionService();
        }
    }

    return true;
  }

  enum ConnectionStates {
       CONNECTION_DISCONNECTED = 1,      // no network connectivity for this connection.
                              // It has either never been connected or has
                              // been fully disconnected.
       CONNECTION_PENDING_CONNECT = 2,   // this connection is in the process of setting
                              // up a connection. The connection setup will be
                              // complete (or fail with an error) when the
                              // application-specified callback is invoked.
       CONNECTION_PENDING_RECEIVE_CONNECT = 3,// connection received an initial
                                              // channel negotiation request, but the
                                              // negotiation has not completed yet.

       CONNECTION_CONNECTED=4,           // The connection is connected to a remote end
                              // point.
       CONNECTION_PENDING_DISCONNECT=5,  // The connection is in the process of
                              // disconnecting from the remote end point.
  };


  /* Create a connection for the application to a remote
     endpoint. The EndPoint argument specifies the location of the remote
     endpoint. It is templatized to enable it to refer to either IP
     addresses and ports, or object identifiers. The
     ConnectionReturnCallbackFunction returns a reference-counted, shared-
     pointer of the Connection that was created. The constructor may or
     may not actually synchronize with the remote endpoint. Instead the
     synchronization may be done when the first stream is created.

     @EndPoint A templatized argument specifying the remote end-point to
               which this connection is connected.

     @ConnectionReturnCallbackFunction A callback function which will be
               called once the connection is created and will provide  a
               reference-counted, shared-pointer to the  connection.
               ConnectionReturnCallbackFunction should have the signature
               void (std::tr1::shared_ptr<Connection>). If the std::tr1::shared_ptr argument
               is NULL, the connection setup failed.

     @return false if it's not possible to create this connection, e.g. if another connection
     is already using the same local endpoint; true otherwise.
  */

  static bool createConnection(ConnectionVariables<EndPointType>* sstConnVars,
                               EndPoint <EndPointType> localEndPoint,
			       EndPoint <EndPointType> remoteEndPoint,
                               ConnectionReturnCallbackFunction cb,
			       StreamReturnCallbackFunction scb)

  {
    boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());

    ConnectionMap& connectionMap = sstConnVars->sConnectionMap;
    if (connectionMap.find(localEndPoint) != connectionMap.end()) {
      SST_LOG(warn, "sConnectionMap.find failed for " << localEndPoint.endPoint.toString() << "\n");

      return false;
    }

    uint32 availableChannel = sstConnVars->getAvailableChannel(localEndPoint.endPoint);

    if (availableChannel == 0)
      return false;

    std::tr1::shared_ptr<Connection>  conn =  std::tr1::shared_ptr<Connection> (
                       new Connection(sstConnVars, localEndPoint, remoteEndPoint));

    connectionMap[localEndPoint] = conn;
    sstConnVars->sConnectionReturnCallbackMap[localEndPoint] = cb;

    lock.unlock();

    conn->setWeakThis(conn);
    conn->setState(CONNECTION_PENDING_CONNECT);

    uint32 payload[1];
    payload[0] = htonl(availableChannel);

    conn->setLocalChannelID(availableChannel);
    conn->sendDataWithAutoAck(payload, sizeof(payload), false);

    return true;
  }

  static bool listen(ConnectionVariables<EndPointType>* sstConnVars, StreamReturnCallbackFunction cb, EndPoint<EndPointType> listeningEndPoint) {
      sstConnVars->getDatagramLayer(listeningEndPoint.endPoint)->listenOn(listeningEndPoint);

    boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());

    StreamReturnCallbackMap& listeningConnectionsCallbackMap = sstConnVars->sListeningConnectionsCallbackMap;

    if (listeningConnectionsCallbackMap.find(listeningEndPoint) != listeningConnectionsCallbackMap.end()){
      return false;
    }

    listeningConnectionsCallbackMap[listeningEndPoint] = cb;

    return true;
  }

  static bool unlisten(ConnectionVariables<EndPointType>* sstConnVars, EndPoint<EndPointType> listeningEndPoint) {
    BaseDatagramLayer<EndPointType>::stopListening(sstConnVars, listeningEndPoint);

    boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());

    sstConnVars->sListeningConnectionsCallbackMap.erase(listeningEndPoint);

    return true;
  }

  void listenStream(uint32 port, StreamReturnCallbackFunction scb) {
    mListeningStreamsCallbackMap[port] = scb;
  }

  void unlistenStream(uint32 port) {
    mListeningStreamsCallbackMap.erase(port);
  }

  /* Creates a stream on top of this connection. The function also queues
     up any initial data that needs to be sent on the stream. The function
     does not return a stream immediately since stream  creation might
     take some time and yet fail in the end. So the function returns without
     synchronizing with the remote host. Instead the callback function
     provides a reference-counted,  shared-pointer to the stream.
     If this connection hasn't synchronized with the remote endpoint yet,
     this function will also take care of doing that.

     @data A pointer to the initial data buffer that needs to be sent on
           this stream. Having this pointer removes the need for the
           application to enqueue data until the stream is actually
           created.
    @port The length of the data buffer.
    @StreamReturnCallbackFunction A callback function which will be
                                 called once the stream is created and
                                 the initial data queued up (or actually
                                 sent?). The function will provide a
                                 reference counted, shared pointer to the
                                 connection. StreamReturnCallbackFunction
                                 should have the signature void (int,std::tr1::shared_ptr<Stream>).

    @return the number of bytes queued from the initial data buffer, or -1 if there was an error.
  */
  virtual int stream(StreamReturnCallbackFunction cb, void* initial_data, int length,
		      uint32 local_port, uint32 remote_port)
  {
    return stream(cb, initial_data, length, local_port, remote_port, 0);
  }

  virtual int stream(StreamReturnCallbackFunction cb, void* initial_data, int length,
                      uint32 local_port, uint32 remote_port, LSID parentLSID)
  {
    USID usid = createNewUSID();
    LSID lsid = ++mNumStreams;

    std::tr1::shared_ptr<Stream<EndPointType> > stream =
      std::tr1::shared_ptr<Stream<EndPointType> >
      ( new Stream<EndPointType>(parentLSID, mWeakThis, local_port, remote_port,  usid, lsid, cb, mSSTConnVars) );
    stream->mWeakThis = stream;
    int numBytesBuffered = stream->init(initial_data, length, false, 0, 0);

    mOutgoingSubstreamMap[lsid]=stream;

    return numBytesBuffered;
  }

  // Implicit version, used when including ack info in self-generated packet,
  // i.e. not in response to packet from other endpoint
  uint64 sendDataWithAutoAck(const void* data, uint32 length, bool isAck) {
      return sendData(data, length, isAck, mLastReceivedSequenceNumber);
  }

  // Explicit version, used when acking direct response to a packet
  uint64 sendData(const void* data, uint32 length, bool isAck, uint64 ack_seqno) {
    boost::mutex::scoped_lock lock(mQueueMutex);

    assert(length <= MAX_PAYLOAD_SIZE);

    uint64 transmitSequenceNumber =  mTransmitSequenceNumber;

    if ( isAck ) {
      Sirikata::Protocol::SST::SSTChannelHeader sstMsg;
      sstMsg.set_channel_id( mRemoteChannelID );
      sstMsg.set_transmit_sequence_number(mTransmitSequenceNumber);
      sstMsg.set_ack_count(1);
      sstMsg.set_ack_sequence_number(ack_seqno);

      sstMsg.set_payload(data, length);

      sendSSTChannelPacket(sstMsg);
    }
    else {
      if (mQueuedSegments.size() < MAX_QUEUED_SEGMENTS) {
        mQueuedSegments.push_back( std::tr1::shared_ptr<ChannelSegment>(
                                   new ChannelSegment(data, length, mTransmitSequenceNumber, ack_seqno) ) );
        // Only service if we're going to be able to send
        // immediately. Otherwise, we must already have outstanding
        // packets waiting for a timeout, in which case this new
        // packet will be dealt with as the existing servicing cycle
        // completes.
        if (mOutstandingSegments.size() <= mCwnd) {
            mInSendingMode = true;
            scheduleConnectionService();
        }
      }
    }

    mTransmitSequenceNumber++;

    return transmitSequenceNumber;
  }

  void setState(int state) {
    mState = state;
  }

  uint8 getState() {
    return mState;
  }

  void setLocalChannelID(uint32 channelID) {
    this->mLocalChannelID = channelID;
  }

  void setRemoteChannelID(uint32 channelID) {
    this->mRemoteChannelID = channelID;
  }

  void setWeakThis( std::tr1::shared_ptr<Connection>  conn) {
    mWeakThis = conn;

    mCheckAliveTimer->setCallback(
        std::tr1::bind(&Connection<EndPointType>::checkIfAlive, this, conn)
    );
    mCheckAliveTimer->wait(Duration::seconds(300));
  }

  USID createNewUSID() {
    uint8 raw_uuid[UUID::static_size];
    for(uint32 ui = 0; ui < UUID::static_size; ui++)
        raw_uuid[ui] = (uint8)rand() % 256;
    UUID id(raw_uuid, UUID::static_size);
    return id;
  }

  void markAcknowledgedPacket(uint64 receivedAckNum) {
    boost::mutex::scoped_lock lock(mOutstandingSegmentsMutex);

    for (std::deque< std::tr1::shared_ptr<ChannelSegment> >::iterator it = mOutstandingSegments.begin();
         it != mOutstandingSegments.end(); it++)
    {
        std::tr1::shared_ptr<ChannelSegment> segment = *it;

        if (!segment) {
          mOutstandingSegments.erase(it);
          it = mOutstandingSegments.begin();
          continue;
        }

        if (segment->mChannelSequenceNumber == receivedAckNum) {
          segment->mAckTime = Timer::now();

          if (mFirstRTO ) {
	         mRTOMicroseconds = ((segment->mAckTime - segment->mTransmitTime).toMicroseconds()) ;
	         mFirstRTO = false;
          }
          else {
            mRTOMicroseconds = CC_ALPHA * mRTOMicroseconds +
              (1.0-CC_ALPHA) * (segment->mAckTime - segment->mTransmitTime).toMicroseconds();
          }

          mOutstandingSegments.erase(it);

          if (rand() % mCwnd == 0)
            mCwnd += 1;

          // We freed up some space in the window. If we have
          // something left to send, trigger servicing.
          if (!mQueuedSegments.empty()) {
              mInSendingMode = true;
              scheduleConnectionService();
          }

          break;
        }
    }
  }

  bool parsePacket(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg )
  {
    Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg =
                       new Sirikata::Protocol::SST::SSTStreamHeader();
    bool parsed = parsePBJMessage(received_stream_msg, received_channel_msg->payload());

    // Easiest to default handled to true here since most of these are trivially
    // handled, they don't require any resources so as long as we look at them
    // we're good. DATA packets are the only ones we need to be careful
    // about. INIT and REPLY can receive data, but we should never have an issue
    // with them as they are the first data, so we'll always have buffer space
    // for them.
    bool handled = true;
    if (received_stream_msg->type() == received_stream_msg->INIT) {
      handleInitPacket(received_channel_msg, received_stream_msg);
    }
    else if (received_stream_msg->type() == received_stream_msg->REPLY) {
      handleReplyPacket(received_channel_msg, received_stream_msg);
    }
    else if (received_stream_msg->type() == received_stream_msg->DATA) {
      handled = handleDataPacket(received_channel_msg, received_stream_msg);
    }
    else if (received_stream_msg->type() == received_stream_msg->ACK) {
      handleAckPacket(received_channel_msg, received_stream_msg);
    }
    else if (received_stream_msg->type() == received_stream_msg->DATAGRAM) {
        handleDatagram(received_channel_msg, received_stream_msg);
    }

    delete received_stream_msg;
    return handled;
  }

  void handleInitPacket(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
      Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg)
  {
    LSID incomingLsid = received_stream_msg->lsid();

    if (mIncomingSubstreamMap.find(incomingLsid) == mIncomingSubstreamMap.end()) {
      if (mListeningStreamsCallbackMap.find(received_stream_msg->dest_port()) !=
	  mListeningStreamsCallbackMap.end())
      {
	//create a new stream
	USID usid = createNewUSID();
	LSID newLSID = ++mNumStreams;

	std::tr1::shared_ptr<Stream<EndPointType> > stream =
	  std::tr1::shared_ptr<Stream<EndPointType> >
	  (new Stream<EndPointType> (received_stream_msg->psid(), mWeakThis,
				     received_stream_msg->dest_port(),
				     received_stream_msg->src_port(),
				     usid, newLSID,
				     NULL, mSSTConnVars));
        stream->mWeakThis = stream;
        stream->init(NULL, 0, true, incomingLsid, received_channel_msg->transmit_sequence_number());

	mOutgoingSubstreamMap[newLSID] = stream;
	mIncomingSubstreamMap[incomingLsid] = stream;

	mListeningStreamsCallbackMap[received_stream_msg->dest_port()](0, stream);

	stream->receiveData(received_channel_msg,
                            received_stream_msg, received_stream_msg->payload().data(),
			    received_stream_msg->bsn(),
			    received_stream_msg->payload().size() );
        stream->receiveAck(
            received_stream_msg,
            received_channel_msg->ack_sequence_number()
        );
      }
      else {
	SST_LOG(warn, mLocalEndPoint.endPoint.toString()  << " not listening to streams at: " << received_stream_msg->dest_port() << "\n");
      }
    }
    else {
        mIncomingSubstreamMap[incomingLsid]->sendReplyPacket(NULL, 0, incomingLsid, received_channel_msg->transmit_sequence_number());
    }
  }

  void handleReplyPacket(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
      Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg)
  {
    LSID incomingLsid = received_stream_msg->lsid();

    if (mIncomingSubstreamMap.find(incomingLsid) == mIncomingSubstreamMap.end()) {
      LSID initiatingLSID = received_stream_msg->rsid();

      if (mOutgoingSubstreamMap.find(initiatingLSID) != mOutgoingSubstreamMap.end()) {
	std::tr1::shared_ptr< Stream<EndPointType> > stream = mOutgoingSubstreamMap[initiatingLSID];
	mIncomingSubstreamMap[incomingLsid] = stream;
        stream->initRemoteLSID(incomingLsid);

	if (stream->mStreamReturnCallback != NULL){
	  stream->mStreamReturnCallback(SST_IMPL_SUCCESS, stream);
          stream->mStreamReturnCallback = NULL;
	  stream->receiveData(received_channel_msg,
                              received_stream_msg, received_stream_msg->payload().data(),
			      received_stream_msg->bsn(),
			      received_stream_msg->payload().size() );
          stream->receiveAck(
              received_stream_msg,
              received_channel_msg->ack_sequence_number()
          );
	}
      }
      else {
	SST_LOG(detailed, "Received reply packet for unknown stream: " <<  initiatingLSID  <<"\n");
      }
    }
  }

  bool handleDataPacket(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
      Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg)
  {
    LSID incomingLsid = received_stream_msg->lsid();

    if (mIncomingSubstreamMap.find(incomingLsid) != mIncomingSubstreamMap.end()) {
      std::tr1::shared_ptr< Stream<EndPointType> > stream_ptr =
	mIncomingSubstreamMap[incomingLsid];
      bool stored = stream_ptr->receiveData(received_channel_msg,
          received_stream_msg,
          received_stream_msg->payload().data(),
          received_stream_msg->bsn(),
          received_stream_msg->payload().size()
      );
      stream_ptr->receiveAck(
          received_stream_msg,
          received_channel_msg->ack_sequence_number()
      );
      return stored;
    }
    // Not sure what to do here if we don't have the stream -- indicate failure
    // and block progress or just allow things to progress?
    return true;
  }

  void handleAckPacket(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
		       Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg)
  {
    //printf("ACK received : offset = %d\n", (int)received_channel_msg->ack_sequence_number() );
    LSID incomingLsid = received_stream_msg->lsid();

    if (mIncomingSubstreamMap.find(incomingLsid) != mIncomingSubstreamMap.end()) {
      std::tr1::shared_ptr< Stream<EndPointType> > stream_ptr =
	mIncomingSubstreamMap[incomingLsid];
      stream_ptr->receiveAck(
          received_stream_msg,
          received_channel_msg->ack_sequence_number()
      );
    }
  }

  void handleDatagram(Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
                      Sirikata::Protocol::SST::SSTStreamHeader* received_stream_msg)
  {
      uint8 msg_flags = received_stream_msg->flags();

      if (msg_flags & Sirikata::Protocol::SST::SSTStreamHeader::CONTINUES) {
          // More data is coming, just store the current data
          mPartialReadDatagrams[received_stream_msg->lsid()].push_back( received_stream_msg->payload() );
      }
      else {
          // Extract dispatch information
          uint32 dest_port = received_stream_msg->dest_port();
          std::vector<ReadDatagramCallback> datagramCallbacks;
          if (mReadDatagramCallbacks.find(dest_port) != mReadDatagramCallbacks.end()) {
              datagramCallbacks = mReadDatagramCallbacks[dest_port];
          }

          // The datagram is all here, just deliver
          PartialPayloadMap::iterator it = mPartialReadDatagrams.find(received_stream_msg->lsid());
          if (it != mPartialReadDatagrams.end()) {
              // Had previous partial packets
              // FIXME this should be more efficient
              std::string full_payload;
              for(PartialPayloadList::iterator pp_it = it->second.begin(); pp_it != it->second.end(); pp_it++)
                  full_payload = full_payload + (*pp_it);
              full_payload = full_payload + received_stream_msg->payload();
              mPartialReadDatagrams.erase(it);
              uint8* payload = (uint8*) full_payload.data();
              uint32 payload_size = full_payload.size();
              for (uint32 i=0 ; i < datagramCallbacks.size(); i++) {
                  datagramCallbacks[i](payload, payload_size);;
              }
          }
          else {
              // Only this part, no need to aggregate into single buffer
              uint8* payload = (uint8*) received_stream_msg->payload().data();
              uint32 payload_size = received_stream_msg->payload().size();
              for (uint32 i=0 ; i < datagramCallbacks.size(); i++) {
                  datagramCallbacks[i](payload, payload_size);
              }
          }
      }


    // And ack
    boost::mutex::scoped_lock lock(mQueueMutex);

    Sirikata::Protocol::SST::SSTChannelHeader sstMsg;
    sstMsg.set_channel_id( mRemoteChannelID );
    sstMsg.set_transmit_sequence_number(mTransmitSequenceNumber);
    sstMsg.set_ack_count(1);
    sstMsg.set_ack_sequence_number(received_channel_msg->transmit_sequence_number());

    sendSSTChannelPacket(sstMsg);

    mTransmitSequenceNumber++;
  }

  void receiveMessageRaw(void* recv_buff, int len) {
    Sirikata::Protocol::SST::SSTChannelHeader* received_msg =
                       new Sirikata::Protocol::SST::SSTChannelHeader();
    bool parsed = parsePBJMessage(received_msg, MemoryReference(recv_buff, len));
    receiveMessage(received_msg);
    delete received_msg;
  }
  // NOTE that this does *not* take ownership of received_msg. The caller is
  // responsible for deleting it.
  void receiveMessage(Sirikata::Protocol::SST::SSTChannelHeader* received_msg) {
      uint64 ack_seqno = received_msg->transmit_sequence_number();

    uint64 receivedAckNum = received_msg->ack_sequence_number();
    markAcknowledgedPacket(receivedAckNum);

    bool handled = false;
    if (mState == CONNECTION_PENDING_CONNECT) {
      mState = CONNECTION_CONNECTED;

      EndPoint<EndPointType> originalListeningEndPoint(mRemoteEndPoint.endPoint, mRemoteEndPoint.port);

      uint32* received_payload = (uint32*) received_msg->payload().data();
      if (received_msg->payload().size()>=sizeof(uint32)*2) {
          setRemoteChannelID( ntohl(received_payload[0]));
          mRemoteEndPoint.port = ntohl(received_payload[1]);
      }

      sendData(received_payload, 0, false, ack_seqno);

      boost::mutex::scoped_lock lock(mSSTConnVars->sStaticMembersLock.getMutex());

      ConnectionReturnCallbackMap& connectionReturnCallbackMap = mSSTConnVars->sConnectionReturnCallbackMap;
      ConnectionMap& connectionMap = mSSTConnVars->sConnectionMap;

      if (connectionReturnCallbackMap.find(mLocalEndPoint) != connectionReturnCallbackMap.end())
      {
        if (connectionMap.find(mLocalEndPoint) != connectionMap.end()) {
          std::tr1::shared_ptr<Connection> conn = connectionMap[mLocalEndPoint];

          connectionReturnCallbackMap[mLocalEndPoint] (SST_IMPL_SUCCESS, conn);
        }
        connectionReturnCallbackMap.erase(mLocalEndPoint);
      }

      handled = true;
    }
    else if (mState == CONNECTION_PENDING_RECEIVE_CONNECT) {
      mState = CONNECTION_CONNECTED;
      handled = true;
    }
    else if (mState == CONNECTION_CONNECTED) {
      if (received_msg->payload().size() > 0)
          handled = parsePacket(received_msg);
      else
          handled = true;
    }

    // We can only update the received seqno that we're going to ack if we
    // actually *fully handled* the packet. This is important, e.g., if we
    // receive a data packet but it had data outside the receive window
    if (handled)
        mLastReceivedSequenceNumber = ack_seqno;
  }

  uint64 getRTOMicroseconds() {
    return mRTOMicroseconds;
  }

  void eraseDisconnectedStream(Stream<EndPointType>* s) {
    mOutgoingSubstreamMap.erase(s->getLSID());
    mIncomingSubstreamMap.erase(s->getRemoteLSID());

    if (mOutgoingSubstreamMap.size() == 0 && mIncomingSubstreamMap.size() == 0) {
      close(true);
    }
  }


  // This is the version of cleanup is used from all the normal methods in Connection
  static void cleanup(std::tr1::shared_ptr<Connection<EndPointType> > conn) {
      // We kill the checkAlive timer in cleanup() and in close()
      // because both paths appear to be possible to hit alone
      // depending on how the connection ends up getting shutdown
      conn->mCheckAliveTimer->cancel();

    conn->mDatagramLayer->unlisten(conn->mLocalEndPoint);

    int connState = conn->mState;

    if (connState == CONNECTION_PENDING_CONNECT || connState == CONNECTION_DISCONNECTED) {
      //Deal with the connection not getting connected with the remote endpoint.
      //This is in contrast to the case where the connection got connected, but
      //the connection's root stream was unable to do so.

       boost::mutex::scoped_lock lock(conn->mSSTConnVars->sStaticMembersLock.getMutex());
       ConnectionReturnCallbackFunction cb = NULL;

       ConnectionReturnCallbackMap& connectionReturnCallbackMap = conn->mSSTConnVars->sConnectionReturnCallbackMap;
       if (connectionReturnCallbackMap.find(conn->localEndPoint()) != connectionReturnCallbackMap.end()) {
         cb = connectionReturnCallbackMap[conn->localEndPoint()];
       }

       std::tr1::shared_ptr<Connection>  failed_conn = conn;

       connectionReturnCallbackMap.erase(conn->localEndPoint());
       conn->mSSTConnVars->sConnectionMap.erase(conn->localEndPoint());

       lock.unlock();


       if (connState == CONNECTION_PENDING_CONNECT && cb ) {
         cb(SST_IMPL_FAILURE, failed_conn);
       }

       conn->mState = CONNECTION_DISCONNECTED;
     }
   }

   // This version should only be called by the destructor!
   void finalCleanup() {
     boost::mutex::scoped_lock lock(mSSTConnVars->sStaticMembersLock.getMutex());

     mDatagramLayer->unlisten(mLocalEndPoint);

     if (mState != CONNECTION_DISCONNECTED) {
         iClose(true);
         mState = CONNECTION_DISCONNECTED;
     }

     mSSTConnVars->releaseChannel(mLocalEndPoint.endPoint, mLocalChannelID);
   }

   static void stopConnections(ConnectionVariables<EndPointType>* sstConnVars) {
       // This just passes stop calls along to all the connections
       boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());
       for(typename ConnectionMap::iterator it = sstConnVars->sConnectionMap.begin(); it != sstConnVars->sConnectionMap.end(); it++)
           it->second->stop();
   }

   static void closeConnections(ConnectionVariables<EndPointType>* sstConnVars) {
       // We have to be careful with this function. Because it is going to free
       // the connections, we have to make sure not to let them get freed where
       // the deleter will modify sConnectionMap while we're still modifying it.
       //
       // Our approach is to just pick out the first connection, make a copy of
       // its shared_ptr to make sure it doesn't get freed until we want it to,
       // remove it from sConnectionMap, and then get rid of the shared_ptr to
       // allow the connection to be freed.
       //
       // Note the careful locking. Connection::~Connection will acquire the
       // sStaticMembersLock, so to avoid deadlocking we grab the shared_ptr,
       // remove it from the list and then only allow the Connection to be
       // destroyed after we've unlocked.
       while(true) {
           ConnectionPtr saved;
           {
               boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());
               if (sstConnVars->sConnectionMap.empty()) break;
               ConnectionMap& connectionMap = sstConnVars->sConnectionMap;

               saved = connectionMap.begin()->second;
               connectionMap.erase(connectionMap.begin());
           }
           // Calling close makes sure we kill the check alive timer,
           // which holds a shared_ptr.
           saved->close(false);
           saved.reset();
       }
   }

   static void handleReceive(ConnectionVariables<EndPointType>* sstConnVars,
                             EndPoint<EndPointType> remoteEndPoint,
                             EndPoint<EndPointType> localEndPoint, void* recv_buffer, int len)
   {
     Sirikata::Protocol::SST::SSTChannelHeader* received_msg = new Sirikata::Protocol::SST::SSTChannelHeader();
     bool parsed = parsePBJMessage(received_msg, MemoryReference(recv_buffer, len));

     uint8 channelID = received_msg->channel_id();

     boost::mutex::scoped_lock lock(sstConnVars->sStaticMembersLock.getMutex());

     ConnectionMap& connectionMap = sstConnVars->sConnectionMap;
     if (connectionMap.find(localEndPoint) != connectionMap.end()) {
       if (channelID == 0) {
 	/*Someone's already connected at this port. Either don't reply or
 	  send back a request rejected message. */

        SST_LOG(info, "Someone's already connected at this port on object " << localEndPoint.endPoint.toString() << "\n");
 	return;
       }
       std::tr1::shared_ptr<Connection<EndPointType> > conn = connectionMap[localEndPoint];

       conn->receiveMessage(received_msg);
     }
     else if (channelID == 0) {
       /* it's a new channel request negotiation protocol
 	        packet ; allocate a new channel.*/

       StreamReturnCallbackMap& listeningConnectionsCallbackMap = sstConnVars->sListeningConnectionsCallbackMap;
       if (listeningConnectionsCallbackMap.find(localEndPoint) != listeningConnectionsCallbackMap.end()) {
         uint32* received_payload = (uint32*) received_msg->payload().data();

         uint32 payload[2];

         uint32 availableChannel = sstConnVars->getAvailableChannel(localEndPoint.endPoint);
         payload[0] = htonl(availableChannel);
         uint32 availablePort = availableChannel; //availableChannel is picked from the same 16-bit
                                                  //address space and has to be unique. So why not use
                                                  //use it to identify the port as well...
         payload[1] = htonl(availablePort);

         EndPoint<EndPointType> newLocalEndPoint(localEndPoint.endPoint, availablePort);
         std::tr1::shared_ptr<Connection>  conn =
                    std::tr1::shared_ptr<Connection>(
                         new Connection(sstConnVars, newLocalEndPoint, remoteEndPoint));


         conn->listenStream(newLocalEndPoint.port, listeningConnectionsCallbackMap[localEndPoint]);
         conn->setWeakThis(conn);
         connectionMap[newLocalEndPoint] = conn;

         conn->setLocalChannelID(availableChannel);
         if (received_msg->payload().size()>=sizeof(uint32)) {
             conn->setRemoteChannelID(ntohl(received_payload[0]));
         }
         conn->setState(CONNECTION_PENDING_RECEIVE_CONNECT);

         conn->sendData(payload, sizeof(payload), false, received_msg->transmit_sequence_number());
       }
       else {
         SST_LOG(warn, "No one listening on this connection\n");
       }
     }

     delete received_msg;
   }

 public:

   virtual ~Connection() {
       // Make sure we've fully cleaned up
       finalCleanup();
   }


  /* Sends the specified data buffer using best-effort datagrams on the
     underlying connection. This may be done using an ephemeral stream
     on top of the underlying connection or some other mechanism (e.g.
     datagram packets sent directly on the underlying  connection).

     @param data the buffer to send
     @param length the length of the buffer
     @param local_port the source port
     @param remote_port the destination port
     @param DatagramSendDoneCallback a callback of type
                                     void (int errCode, void*)
                                     which is called when queuing
                                     the datagram failed or succeeded.
                                     'errCode' contains SST_IMPL_SUCCESS or SST_IMPL_FAILURE
                                     while the 'void*' argument is a pointer
                                     to the buffer that was being sent.

     @return false if there's an immediate failure while enqueuing the datagram; true, otherwise.
  */
  virtual bool datagram( void* data, int length, uint32 local_port, uint32 remote_port,
			 DatagramSendDoneCallback cb) {
    int currOffset = 0;

    if (mState == CONNECTION_DISCONNECTED
     || mState == CONNECTION_PENDING_DISCONNECT)
    {
      if (cb != NULL) {
        cb(SST_IMPL_FAILURE, data);
      }
      return false;
    }

    LSID lsid = ++mNumStreams;

    while (currOffset < length) {
        // Because the header is variable size, we have to have this
        // somewhat annoying logic to ensure we come in under the
        // budget.  We start out with an extra 28 bytes as buffer.
        // Hopefully this is usually enough, and is based on the
        // current required header fields, their sizes, and overhead
        // from protocol buffers encoding.  In the worst case, we end
        // up being too large and have to iterate, working with less
        // data over time.
        int header_buffer = 28;
        while(true) {
            int buffLen;
            bool continues;
            if (length-currOffset > (MAX_PAYLOAD_SIZE-header_buffer)) {
                buffLen = MAX_PAYLOAD_SIZE-header_buffer;
                continues = true;
            }
            else {
                buffLen = length-currOffset;
                continues = false;
            }


            Sirikata::Protocol::SST::SSTStreamHeader sstMsg;
            sstMsg.set_lsid( lsid );
            sstMsg.set_type(sstMsg.DATAGRAM);

            uint8 flags = 0;
            if (continues)
                flags = flags | Sirikata::Protocol::SST::SSTStreamHeader::CONTINUES;
            sstMsg.set_flags(flags);

            sstMsg.set_window( (unsigned char)10 );
            sstMsg.set_src_port(local_port);
            sstMsg.set_dest_port(remote_port);

            sstMsg.set_payload( ((uint8*)data)+currOffset, buffLen);

            std::string buffer = serializePBJMessage(sstMsg);

            // If we're not within the payload size, we need to
            // increase our buffer space and try again
            if (buffer.size() > MAX_PAYLOAD_SIZE) {
                header_buffer += 10;
                continue;
            }

            sendDataWithAutoAck(  buffer.data(), buffer.size(), false );

            currOffset += buffLen;
            // If we got to the send, we can break out of the loop
            break;
        }
    }

    if (cb != NULL) {
      //invoke the callback function
      cb(SST_IMPL_SUCCESS, data);
    }

    return true;
  }

  /*
    Register a callback which will be called when there is a datagram
    available to be read.

    @param port the local port on which to listen for datagrams.
    @param ReadDatagramCallback a function of type "void (uint8*, int)"
           which will be called when a datagram is available. The
           "uint8*" field will be filled up with the received datagram,
           while the 'int' field will contain its size.
    @return true if the callback was successfully registered.
  */
  virtual bool registerReadDatagramCallback(uint32 port, ReadDatagramCallback cb) {
    if (mReadDatagramCallbacks.find(port) == mReadDatagramCallbacks.end()) {
      mReadDatagramCallbacks[port] = std::vector<ReadDatagramCallback>();
    }

    mReadDatagramCallbacks[port].push_back(cb);

    return true;
  }

  /*
    Register a callback which will be called when there is a new
    datagram available to be read. In other words, datagrams we have
    seen previously will not trigger this callback.

    @param ReadDatagramCallback a function of type "void (uint8*, int)"
           which will be called when a datagram is available. The
           "uint8*" field will be filled up with the received datagram,
           while the 'int' field will contain its size.
    @return true if the callback was successfully registered.
  */
  virtual bool registerReadOrderedDatagramCallback( ReadDatagramCallback cb )  {
     return true;
  }

  /** Stops the connection. This isn't the same as close/cleanup. Rather, it's
   * an indicator that the Service running SST needs to stop. This should try to
   * start a clean, quick, but graceful stop.
   */
  void stop() {
      // Request that all streams stop. This may hit some streams twice since
      // they may be in both incoming and outgoing stream lists
      for(typename LSIDStreamMap::iterator it = mIncomingSubstreamMap.begin(); it != mIncomingSubstreamMap.end(); it++)
          it->second->stop();
      for(typename LSIDStreamMap::iterator it = mOutgoingSubstreamMap.begin(); it != mOutgoingSubstreamMap.end(); it++)
          it->second->stop();

      // Also, don't hold ourselves alive, no need to track liveness anymore
      mCheckAliveTimer->cancel();
  }

  /*  Closes the connection.

      @param force if true, the connection is closed forcibly and
             immediately. Otherwise, the connection is closed
             gracefully and all outstanding packets are sent and
             acknowledged. Note that even in the latter case,
             the function returns without synchronizing with the
             remote end point.
  */
  virtual void close(bool force) {
      boost::mutex::scoped_lock lock(mSSTConnVars->sStaticMembersLock.getMutex());
      iClose(force);
  }

  /* Internal, non-locking implementation of close().
     Lock mSSTConnVars->sStaticMembersLock before calling this function */
  virtual void iClose(bool force) {
      // We kill the checkAlive timer in cleanup() and in close()
      // because both paths appear to be possible to hit alone
      // depending on how the connection ends up getting shutdown
      mCheckAliveTimer->cancel();

    /* (mState != CONNECTION_DISCONNECTED) implies close() wasnt called
       through the destructor. */
    if (force && mState != CONNECTION_DISCONNECTED) {
      mSSTConnVars->sConnectionMap.erase(mLocalEndPoint);
    }

    if (force) {
      mState = CONNECTION_DISCONNECTED;
    }
    else  {
      mState = CONNECTION_PENDING_DISCONNECT;
    }
  }



  /*
    Returns the local endpoint to which this connection is bound.

    @return the local endpoint.
  */
  virtual EndPoint <EndPointType> localEndPoint()  {
    return mLocalEndPoint;
  }

  /*
    Returns the remote endpoint to which this connection is connected.

    @return the remote endpoint.
  */
  virtual EndPoint <EndPointType> remoteEndPoint() {
    return mRemoteEndPoint;
  }

};


class StreamBuffer{
public:

  uint8* mBuffer;
  uint32 mBufferLength;
  uint64 mOffset;

  Time mTransmitTime;
  Time mAckTime;

  StreamBuffer(const uint8* data, uint32 len, uint64 offset) :
    mTransmitTime(Time::null()), mAckTime(Time::null())
  {
    mBuffer = new uint8[len+1];

    if (len > 0) {
      memcpy(mBuffer,data,len);
    }

    mBufferLength = len;
    mOffset = offset;
  }

  ~StreamBuffer() {
      delete []mBuffer;
  }

    // This doesn't check the data, just that the StreamBuffers
    // represent the same portion of the stream.
    bool operator==(const StreamBuffer& rhs) {
        return (mOffset == rhs.mOffset && mBufferLength == rhs.mBufferLength);
    }
};
typedef std::tr1::shared_ptr<StreamBuffer> StreamBufferPtr;

// Tracks segments that have been received in a stream, handling
// merging them so we can deliver as much data in each callback as
// possible.
class ReceivedSegmentList {
public:

    // Represents a range in the stream of data: start byte + length
    typedef std::pair<int64, int64> SegmentRange;
    static int64 StartByte(const SegmentRange& sr) {
        return sr.first;
    }
    // Note that this is 'end' in the container sense, 1 past the last
    // valid byte
    static int64 EndByte(const SegmentRange& sr) {
        return sr.first + sr.second;
    }
    static int64 Length(const SegmentRange& sr) {
        return sr.second;
    }

    // Insert (or update) a now valid range of bytes in the
    // stream. This updates our list, merging segments if necessary.
    void insert(int64 offset, int64 length) {
        // Simple case: empty list we can just insert directly
        if (mSegments.empty()) {
            mSegments.push_back(SegmentRange(offset, length));
            return;
        }

        // Might be able to insert it at the front, which the following loop
        // doesn't catch properly
        if ((offset+length) <= EndByte(mSegments.front())) {
            bool merge_first = (offset+length) >= StartByte(mSegments.front());
            if (merge_first) {
                // Could be pure overlap. Only need to do anything if this
                // extends the starting point of the segment to be earlier
                if (offset < mSegments.front().first) {
                    mSegments.front().first = offset;
                    mSegments.front().second += length;
                }
            }
            else {
                // No merge, just add a new one
                mSegments.push_front(SegmentRange(offset, length));
            }
            // Either way, we've used this update, so we can skip the rest.
            return;
        }

        // Figure out what entry we we can insert after
        SegmentList::iterator it, next_it;
        for(next_it = mSegments.begin(), it = next_it++; true; it++, next_it++) {
            assert(it != mSegments.end());
            // If the segments start byte is in the current segment,
            // we've got some overlap
            if (offset >= StartByte(*it) && offset < EndByte(*it)) {
                // Currently we only handle complete overlap. Since we
                // don't ever re-segment things currently, this should
                // be fine. However, we could have merged, so the
                // complete overlap on this inserted segment could
                // only cover a part of the segment we overlap, so we
                // still have to be careful with this assertion
                assert(offset + length <= EndByte(*it));
                // Nothing to do since it's already registered
                return;
            }

            // Otherwise we're looking for a place to insert between
            // other segments, and then possibly merging. We need to
            // have overlap of an empty region, i.e. between the
            // current and next segments.
            if (offset >= EndByte(*it) &&
                (next_it == mSegments.end() || (offset+length) <= StartByte(*next_it)))
            {
                bool merge_previous = (offset == EndByte(*it));
                bool merge_next = (next_it != mSegments.end() && (offset+length) == StartByte(*next_it));
                if (merge_previous) {
                    // Merge previous, might also need to merge next
                    if (merge_next) {
                        // Crosses all three, merge into first, remove second
                        it->second = (it->second + length + next_it->second);
                        mSegments.erase(next_it);
                    }
                    else {
                        // Crosses just the two, merge in and no insert/remove
                        it->second = (it->second + length);
                    }
                }
                else if (merge_next) {
                    // Or only merge next. Need to use start from
                    // inserted segment and combine their lengths
                    it->first = offset;
                    it->second = (it->second + length);
                }
                else {
                    // No merging, just insert (before next_it).
                    mSegments.insert(next_it, SegmentRange(offset, length));
                }
                return;
            }

            // Otherwise, we need to keep moving along to find the
            // right spot
        }
    }

    // Get the range of ready bytes given that we have a specific next
    // expected byte. skipCheckLength lets you indicate that you know
    // you've already added a certain number of bytes that don't need
    // to be accounted for here because you just received them. This
    // also *removes this data* from the segment list so it should
    // only be called when you're going to deliver data.
    SegmentRange readyRange(int64 nextStartByte, int64 skipCheckLength) {
        // Start looking at our data from after the skip data
        int64 skipStartByte = nextStartByte + skipCheckLength;
        // In case the skip data covers any of our segments, pop
        // things off the front of the list as long as they are
        // completely covered.
        while(!mSegments.empty() && EndByte(mSegments.front()) <= skipStartByte)
            mSegments.pop_front();

        // If we don't have any ready segments, we can only account for the
        // skipped data. Otherwise, we're guaranteed only partial coverage,
        // contiguous, or doesn't reach the first segment. First, handle no
        // ready segments and non-contiguous since it's simple -- the start of
        // the first data we know about is beyond the start byte.
        if (mSegments.empty() ||
            (mSegments.front().first > skipStartByte))
        {
            return SegmentRange(nextStartByte, skipCheckLength);
        }

        // Then we only have overlap or just contiguous, in which case
        // we span from the start of the skipData (i.e. nextStartByte)
        // to the end of the next segment.
        SegmentRange ready = mSegments.front();
        mSegments.pop_front();
        // The next segment shouldn't be contiguous with the ready
        // range. Note >, not >= since == would imply it is
        // contiguous. This is really just a sanity check on the
        // SegmentRange insertion code.
        assert(mSegments.empty() || mSegments.front().first > EndByte(ready));
        SegmentRange merged_ready(nextStartByte, EndByte(ready)-nextStartByte);
        return merged_ready;
    };

private:
    // Lists/deques aren't particularly fast, but they let us muck with
    // the contents easily when we want to insert ranges we've
    // received, merge entries that a new entry made contiguous, etc.,
    // and this list shouldn't ever get very big anyway. It ideally is
    // only at most one entry at a time and even if we drop packets,
    // should stay small as segments are merged.
    typedef std::deque<SegmentRange> SegmentList;
    SegmentList mSegments;
}; // class ReceivedSegmentList

template <class EndPointType>
class SIRIKATA_EXPORT Stream  {
public:
    typedef std::tr1::shared_ptr<Stream> Ptr;
    typedef Ptr StreamPtr;
    typedef Connection<EndPointType> ConnectionType;
    typedef std::tr1::shared_ptr<ConnectionType> ConnectionPtr;
    typedef EndPoint<EndPointType> EndpointType;

    typedef CallbackTypes<EndPointType> CBTypes;
    typedef typename CBTypes::StreamReturnCallbackFunction StreamReturnCallbackFunction;
    typedef typename CBTypes::ReadCallback ReadCallback;

    typedef std::tr1::unordered_map<EndPoint<EndPointType>, StreamReturnCallbackFunction, typename EndPoint<EndPointType>::Hasher> StreamReturnCallbackMap;

   enum StreamStates {
       DISCONNECTED = 1,
       CONNECTED=2,
       PENDING_DISCONNECT=3,
       PENDING_CONNECT=4,
       NOT_FINISHED_CONSTRUCTING__CALL_INIT
     };



   virtual ~Stream() {
    close(true);

    delete [] mInitialData;
    delete [] mReceiveBuffer;

    mConnection.reset();
  }

  bool connected() { return mConnected; }

  static bool connectStream(ConnectionVariables<EndPointType>* sstConnVars,
                            EndPoint <EndPointType> localEndPoint,
			    EndPoint <EndPointType> remoteEndPoint,
			    StreamReturnCallbackFunction cb)
  {
      if (localEndPoint.port == 0) {
          typename BaseDatagramLayer<EndPointType>::Ptr bdl = sstConnVars->getDatagramLayer(localEndPoint.endPoint);
          if (!bdl) {
              SST_LOG(error,"Tried to connect stream without calling createDatagramLayer for the endpoint.");
              return false;
          }
          localEndPoint.port = bdl->getUnusedPort(localEndPoint.endPoint);
      }

      StreamReturnCallbackMap& streamReturnCallbackMap = sstConnVars->mStreamReturnCallbackMap;
      if (streamReturnCallbackMap.find(localEndPoint) != streamReturnCallbackMap.end()) {
        return false;
      }

      streamReturnCallbackMap[localEndPoint] = cb;

      bool result = Connection<EndPointType>::createConnection(sstConnVars,
                                                               localEndPoint,
                                                               remoteEndPoint,
                                                               connectionCreated, cb);
      return result;
  }

  /*
    Start listening for top-level streams on the specified end-point. When
    a new top-level stream connects at the given endpoint, the specified
    callback function is invoked handing the object a top-level stream.
    @param cb the callback function invoked when a new stream is created
    @param listeningEndPoint the endpoint where SST will accept new incoming
           streams.
    @return false, if its not possible to listen to this endpoint (e.g. if listen
            has already been called on this endpoint); true otherwise.
  */
  static bool listen(ConnectionVariables<EndPointType>* sstConnVars, StreamReturnCallbackFunction cb, EndPoint <EndPointType> listeningEndPoint) {
    return Connection<EndPointType>::listen(sstConnVars, cb, listeningEndPoint);
  }

  static bool unlisten(ConnectionVariables<EndPointType>* sstConnVars, EndPoint <EndPointType> listeningEndPoint) {
    return Connection<EndPointType>::unlisten(sstConnVars, listeningEndPoint);
  }

  /*
    Start listening for child streams on the specified port. A remote stream
    can only create child streams under this stream if this stream is listening
    on the port specified for the child stream.

    @param scb the callback function invoked when a new stream is created
    @param port the endpoint where SST will accept new incoming
           streams.
  */
  void listenSubstream(uint32 port, StreamReturnCallbackFunction scb) {
    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    if (!conn) {
      scb(SST_IMPL_FAILURE, StreamPtr() );
      return;
    }

    conn->listenStream(port, scb);
  }

  void unlistenSubstream(uint32 port) {
    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();

    if (conn)
      conn->unlistenStream(port);
  }

  /* Writes data bytes to the stream. If not all bytes can be transmitted
     immediately, they are queued locally until ready to transmit.
     @param data the buffer containing the bytes to be written
     @param len the length of the buffer
     @return the number of bytes written or enqueued, or -1 if an error
             occurred
  */
  virtual int write(const uint8* data, int len) {
    if (mState == DISCONNECTED || mState == PENDING_DISCONNECT) {
      return -1;
    }

    boost::mutex::scoped_lock lock(mQueueMutex);
    int count = 0;
    // We only need to schedule servicing when the packet queue
    // goes from empty to non-empty since we should already be working
    // on sending data if it wasn't
    bool was_empty = mQueuedBuffers.empty();

    if (len <= MAX_PAYLOAD_SIZE) {
      if (mCurrentQueueLength+len > MAX_QUEUE_LENGTH) {
	return 0;
      }
      mQueuedBuffers.push_back( std::tr1::shared_ptr<StreamBuffer>(new StreamBuffer(data, len, mNumBytesSent)) );
      mCurrentQueueLength += len;
      mNumBytesSent += len;

      if (was_empty)
          scheduleStreamService();

      return len;
    }
    else {
      int currOffset = 0;
      while (currOffset < len) {
	int buffLen = (len-currOffset > MAX_PAYLOAD_SIZE) ?
	              MAX_PAYLOAD_SIZE :
	              (len-currOffset);

	if (mCurrentQueueLength + buffLen > MAX_QUEUE_LENGTH) {
	  break;
	}

	mQueuedBuffers.push_back( std::tr1::shared_ptr<StreamBuffer>(new StreamBuffer(data+currOffset, buffLen, mNumBytesSent)) );
	currOffset += buffLen;
	mCurrentQueueLength += buffLen;
	mNumBytesSent += buffLen;

	count++;
      }

      if (was_empty && currOffset > 0)
          scheduleStreamService();

      return currOffset;
    }

    return -1;
  }

#if SIRIKATA_PLATFORM != SIRIKATA_PLATFORM_WINDOWS
  /* Gathers data from the buffers described in 'vec',
     which is taken to be 'count' structures long, and
     writes them to the stream. As each buffer is
     written, it moves on to the next. If not all bytes
     can be transmitted immediately, they are queued
     locally until ready to transmit.

     The return value is a count of bytes written.

     @param vec the array containing the iovec buffers to be written
     @param count the number of iovec buffers in the array
     @return the number of bytes written or enqueued, or -1 if an error
             occurred
  */
  virtual int writev(const struct iovec* vec, int count) {
    int totalBytesWritten = 0;

    for (int i=0; i < count; i++) {
      int numWritten = write( (const uint8*) vec[i].iov_base, vec[i].iov_len);

      if (numWritten < 0) return -1;

      totalBytesWritten += numWritten;

      if (numWritten == 0) {
	return totalBytesWritten;
      }
    }

    return totalBytesWritten;
  }
#endif

  /*
    Register a callback which will be called when there are bytes to be
    read from the stream.

    @param ReadCallback a function of type "void (uint8*, int)" which will
           be called when data is available. The "uint8*" field will be filled
           up with the received data, while the 'int' field will contain
           the size of the data.
    @return true if the callback was successfully registered.
  */
  virtual bool registerReadCallback( ReadCallback callback) {
    mReadCallback = callback;

    boost::recursive_mutex::scoped_lock lock(mReceiveBufferMutex);
    sendToApp(0);

    return true;
  }


  /** Stops the stream. This isn't semantically the same as a request to
   * close. Rather, it's an indicator that the Service running SST needs to
   * stop. This should try to start a clean, quick, but graceful stop.
   */
  void stop() {
      close(false);
  }

  /* Close this stream. If the 'force' parameter is 'false',
     all outstanding data is sent and acknowledged before the stream is closed.
     Otherwise, the stream is closed immediately and outstanding data may be lost.
     Note that in the former case, the function will still return immediately, changing
     the state of the connection PENDING_DISCONNECT without necessarily talking to the
     remote endpoint.
     @param force use false if the stream should be gracefully closed, true otherwise.
     @return  true if the stream was successfully closed.

  */
  virtual bool close(bool force) {
      std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    if (force) {
      mConnected = false;
      mState = DISCONNECTED;

      if (conn) {
        conn->eraseDisconnectedStream(this);
      }

      // In both cases, we can stop keepalives
      mKeepAliveTimer->cancel();
      return true;
    }
    else {
      mState = PENDING_DISCONNECT;
      scheduleStreamService();
      // In both cases, we can stop keepalives
      mKeepAliveTimer->cancel();
      return true;
    }
  }

  /*
    Sets the priority of this stream.
    As in the original SST interface, this implementation gives strict preference to
    streams with higher priority over streams with lower priority, but it divides
    available transmit bandwidth evenly among streams with the same priority level.
    All streams have a default priority level of zero.
    @param the new priority level of the stream.
  */
  virtual void setPriority(int pri) {

  }

  /*Returns the stream's current priority level.
    @return the stream's current priority level
  */
  virtual int priority() {
    return 0;
  }

  /* Returns the top-level connection that created this stream.
     @return a pointer to the connection that created this stream.
  */
  virtual std::tr1::weak_ptr<Connection<EndPointType> > connection() {
    return mConnection;
  }

  /* Creates a child stream. The function also queues up
     any initial data that needs to be sent on the child stream. The function does not
     return a stream immediately since stream creation might take some time and
     yet fail in the end. So the function returns without synchronizing with the
     remote host. Instead the callback function provides a reference-counted,
     shared-pointer to the stream. If this connection hasn't synchronized with
     the remote endpoint yet, this function will also take care of doing that.

     @param data A pointer to the initial data buffer that needs to be sent on this stream.
         Having this pointer removes the need for the application to enqueue data
         until the stream is actually created.
     @param port The length of the data buffer.
     @param local_port the local port to which the child stream will be bound.
     @param remote_port the remote port to which the child stream should connect.
     @param StreamReturnCallbackFunction A callback function which will be called once the
                                  stream is created and the initial data queued up
                                  (or actually sent?). The function will provide  a
                                  reference counted, shared pointer to the  connection.

     @return the number of bytes actually buffered from the initial data buffer specified, or
     -1 if an error occurred.
  */
  virtual int createChildStream(StreamReturnCallbackFunction cb, void* data, int length,
				 uint32 local_port, uint32 remote_port)
  {
    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    if (conn) {
      return conn->stream(cb, data, length, local_port, remote_port, mLSID);
    }

    return -1;
  }

  /*
    Returns the local endpoint to which this connection is bound.

    @return the local endpoint.
  */
  virtual EndPoint <EndPointType> localEndPoint()  {
    return mLocalEndPoint;
  }

  /*
    Returns the remote endpoint to which this connection is bound.

    @return the remote endpoint.
  */
  virtual EndPoint <EndPointType> remoteEndPoint()  {
    return mRemoteEndPoint;
  }

  virtual uint8 getState() {
    return mState;
  }

  const Context* getContext() {
    if (mContext == NULL) {
      std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
      assert(conn);

      mContext = conn->getContext();
    }

    return mContext;
  }

private:
  Stream(LSID parentLSID, std::tr1::weak_ptr<Connection<EndPointType> > conn,
	 uint32 local_port, uint32 remote_port,
	 USID usid, LSID lsid, StreamReturnCallbackFunction cb, ConnectionVariables<EndPointType>* sstConnVars)
    :
    mState(NOT_FINISHED_CONSTRUCTING__CALL_INIT),
    mLocalPort(local_port),
    mRemotePort(remote_port),
    mParentLSID(parentLSID),
    mConnection(conn),mContext(NULL),
    mUSID(usid),
    mLSID(lsid),
    mRemoteLSID(-1),
    MAX_PAYLOAD_SIZE(1000),
    MAX_QUEUE_LENGTH(4000000),
    MAX_RECEIVE_WINDOW(GetOptionValue<uint32>(OPT_SST_DEFAULT_WINDOW_SIZE)),
    mFirstRTO(true),
    mStreamRTOMicroseconds(2000000),
    FL_ALPHA(0.8),
    mTransmitWindowSize(MAX_RECEIVE_WINDOW),
    mReceiveWindowSize(MAX_RECEIVE_WINDOW),
    mNumOutstandingBytes(0),
    mNextByteExpected(0),
    mLastContiguousByteReceived(-1),
    mLastSendTime(Time::null()),
    mLastReceiveTime(Time::null()),
    mStreamReturnCallback(cb),
    mConnected (false),
    MAX_INIT_RETRANSMISSIONS(5),
    mSSTConnVars(sstConnVars),
      mKeepAliveTimer(
          Network::IOTimer::create(
              getContext()->mainStrand
              // Can't create callback yet because we need mWeathThis
          )
      ),
      mServiceTimer(
          Network::IOTimer::create(
              getContext()->mainStrand,
              std::tr1::bind(&Stream<EndPointType>::serviceStreamNoReturn, this)
          )
      ),
      mIsAsyncServicing(false),
      mServiceStrongRef(), // Should start NULL
      mServiceStrongConnRef() // Should start NULL
  {
    mInitialData = NULL;
    mInitialDataLength = 0;

    mReceiveBuffer = NULL;

    mQueuedBuffers.clear();
    mCurrentQueueLength = 0;

    std::tr1::shared_ptr<Connection<EndPointType> > locked_conn = mConnection.lock();
    mRemoteEndPoint = EndPoint<EndPointType> (locked_conn->remoteEndPoint().endPoint, mRemotePort);
    mLocalEndPoint = EndPoint<EndPointType> (locked_conn->localEndPoint().endPoint, mLocalPort);

    // Continues in init, when we have mWeakThis set
  }

  // ack_seqno is only required when the stream is remotely initiated
  int init(void* initial_data, uint32 length, bool remotelyInitiated, LSID remoteLSID, uint64 ack_seqno) {
    mNumInitRetransmissions = 1;
    if (remotelyInitiated) {
        mRemoteLSID = remoteLSID;
        mConnected = true;
        mState = CONNECTED;
    }
    else {
        mConnected = false;
        mState = PENDING_CONNECT;
    }

    mInitialDataLength = (length <= MAX_PAYLOAD_SIZE) ? length : MAX_PAYLOAD_SIZE;
    int numBytesBuffered = mInitialDataLength;

    if (initial_data != NULL) {
      mInitialData = new uint8[mInitialDataLength];

      memcpy(mInitialData, initial_data, mInitialDataLength);
    }
    else {
      mInitialData = new uint8[1];
      mInitialDataLength = 0;
    }

    if (remotelyInitiated) {
        sendReplyPacket(mInitialData, mInitialDataLength, remoteLSID, ack_seqno);
    }
    else {
      sendInitPacket(mInitialData, mInitialDataLength);
    }

    mNumBytesSent = mInitialDataLength;

    if (length > mInitialDataLength) {
      int writeval = write( ((uint8*)initial_data) + mInitialDataLength, length - mInitialDataLength);

      if (writeval >= 0) {
        numBytesBuffered += writeval;
      }
    }

    /** Post a keep-alive task...  **/
    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    if (conn) {
        assert(mWeakThis.lock());
        mKeepAliveTimer->setCallback(
              std::tr1::bind(&Stream<EndPointType>::sendKeepAlive, this, mWeakThis, conn)
        );
        mKeepAliveTimer->wait(Duration::seconds(60));
    }

    return numBytesBuffered;
  }

  uint8* receiveBuffer() {
      if (mReceiveBuffer == NULL)
          mReceiveBuffer = new uint8[MAX_RECEIVE_WINDOW];
      return mReceiveBuffer;
  }

  void initRemoteLSID(LSID remoteLSID) {
      mRemoteLSID = remoteLSID;
  }

  void sendKeepAlive(std::tr1::weak_ptr<Stream<EndPointType> > wstrm, std::tr1::shared_ptr<Connection<EndPointType> > conn) {
      std::tr1::shared_ptr<Stream<EndPointType> > strm = wstrm.lock();
      if (!strm) return;

    if (mState == DISCONNECTED || mState == PENDING_DISCONNECT) {
      close(true);
      return;
    }

    uint8 buf[1];

    write(buf, 0);

    mKeepAliveTimer->wait(Duration::seconds(60));
  }

  static void connectionCreated( int errCode, std::tr1::shared_ptr<Connection<EndPointType> > c) {
    StreamReturnCallbackMap& streamReturnCallbackMap = c->mSSTConnVars->mStreamReturnCallbackMap;
    assert(streamReturnCallbackMap.find(c->localEndPoint()) != streamReturnCallbackMap.end());

    if (errCode != SST_IMPL_SUCCESS) {

      StreamReturnCallbackFunction cb = streamReturnCallbackMap[c->localEndPoint()];
      streamReturnCallbackMap.erase(c->localEndPoint());

      cb(SST_IMPL_FAILURE, StreamPtr() );

      return;
    }

    c->stream(streamReturnCallbackMap[c->localEndPoint()], NULL , 0,
	      c->localEndPoint().port, c->remoteEndPoint().port);

    streamReturnCallbackMap.erase(c->localEndPoint());
  }

  void serviceStreamNoReturn() {
      serviceStream();
  }

  /* Returns false only if this is the root stream of a connection and it was
     unable to connect. In that case, the connection for this stream needs to
     be closed and the 'false' return value is an indication of this for
     the underlying connection. */

  bool serviceStream() {
      std::pair<StreamPtr, ConnectionPtr> refs = startServicing();
      StreamPtr strm = refs.first;
      ConnectionPtr conn = refs.second;

    assert(strm.get() == this);

    const Time curTime = Timer::now();

    if ( (curTime - mLastReceiveTime).toSeconds() > 300 && mLastReceiveTime != Time::null())
    {
      close(true);
      return true;
    }

    if (mState == DISCONNECTED) return true;

    if (mState != CONNECTED && mState != PENDING_DISCONNECT) {

      if (!mConnected && mNumInitRetransmissions < MAX_INIT_RETRANSMISSIONS ) {
        sendInitPacket(mInitialData, mInitialDataLength);
	mLastSendTime = curTime;
	mNumInitRetransmissions++;
	return true;
      }

      mInitialDataLength = 0;

      if (!mConnected) {
        std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
        assert(conn);

        mSSTConnVars->mStreamReturnCallbackMap.erase(conn->localEndPoint());

	// If this is the root stream that failed to connect, close the
	// connection associated with it as well.
	if (mParentLSID == 0) {
          conn->close(true);
          Connection<EndPointType>::cleanup(conn);
	}

	//send back an error to the app by calling mStreamReturnCallback
	//with an error code.
        if (mStreamReturnCallback) {
            mStreamReturnCallback(SST_IMPL_FAILURE, StreamPtr());
            mStreamReturnCallback = NULL;
        }

        conn->eraseDisconnectedStream(this);
        mState = DISCONNECTED;

	return false;
      }
      else {
	mState = CONNECTED;
        // Schedule another servicing immediately in case any other operations
        // should occur, e.g. sending data which was added after the initial
        // connection request.
        scheduleStreamService();
      }
    }
    else {
        //if the stream has been waiting for an ACK for > 2*mStreamRTOMicroseconds,
        //resend the unacked packets. We don't actually check if we
        //have anything to ack here, that happens in resendUnackedPackets. Also,
        //'resending' really just means sticking them back at the front of
        //mQueuedBuffers, so the code that follows and actually sends data will
        //ensure that we trigger a re-servicing sometime in the future.
        if ( mLastSendTime != Time::null()
             && (curTime - mLastSendTime).toMicroseconds() > 2*mStreamRTOMicroseconds)
        {
            resendUnackedPackets();
            mLastSendTime = curTime;
        }

	boost::mutex::scoped_lock lock(mQueueMutex);

	if (mState == PENDING_DISCONNECT &&
	    mQueuedBuffers.empty()  &&
	    mWaitingForAcks.empty() )
	{
	    mState = DISCONNECTED;

            std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
            assert(conn);

            conn->eraseDisconnectedStream(this);

	    return true;
	}

        bool sentSomething = false;
	while ( !mQueuedBuffers.empty() ) {
	  std::tr1::shared_ptr<StreamBuffer> buffer = mQueuedBuffers.front();

          // If we managed to get an ack late, then we will not have
          // been able to remove the actual buffer that got requeued
          // for a retry (if it hadn't been processed again). However,
          // we only ever mark the ack time when we've actually seen
          // it acked, so we can use that to check if we really still
          // need to send it.
          if (buffer->mAckTime != Time::null()) {
              mQueuedBuffers.pop_front();
              mCurrentQueueLength -= buffer->mBufferLength;
              continue;
          }

	  if (mTransmitWindowSize < buffer->mBufferLength) {
	    break;
	  }

	  uint64 channelID = sendDataPacket(buffer->mBuffer,
					    buffer->mBufferLength,
					    buffer->mOffset
					    );
          buffer->mTransmitTime = curTime;
          sentSomething = true;

          // On the first send (or during a resend where we get a new
          // channel ID) we only mark this as waiting for an ack using
          // the specified channel segment ID.
          assert(mWaitingForAcks.find(channelID) == mWaitingForAcks.end());
          mWaitingForAcks[channelID] = buffer;

	  mQueuedBuffers.pop_front();
	  mCurrentQueueLength -= buffer->mBufferLength;
	  mLastSendTime = curTime;

	  assert(buffer->mBufferLength <= mTransmitWindowSize);
	  mTransmitWindowSize -= buffer->mBufferLength;
	  mNumOutstandingBytes += buffer->mBufferLength;
	}

        // Either we sent something and we need to setup a retransmit
        // timeout or we passed through without sending anything
        // because the transmit window is full, in which case we also
        // need to continue the retransmit timeout.
        if (sentSomething || !mQueuedBuffers.empty()) {
            // Since we might run through this after a timeout already
            // started, make sure we target the exact time, adjusting
            // for already elapsed time since the last received
            // packet.
            assert(Duration::microseconds(2*mStreamRTOMicroseconds) >= (curTime - mLastSendTime));
            Duration timeout = Duration::microseconds(2*mStreamRTOMicroseconds) - (curTime - mLastSendTime);
            scheduleStreamService(timeout);
        }
    }

    return true;
  }

  inline void resendUnackedPackets() {
    boost::mutex::scoped_lock lock(mQueueMutex);

    // Requeue outstanding packets for transmission
    for(typename ChannelToBufferMap::const_reverse_iterator it = mWaitingForAcks.rbegin(),
            it_end = mWaitingForAcks.rend(); it != it_end; it++)
    {
        // Put back in queue
        mQueuedBuffers.push_front(it->second);
        mCurrentQueueLength += it->second->mBufferLength;
        // Save with old channelID to the graveyard
        mUnackedGraveyard[it->first] = it->second;
        /*printf("On %d, resending unacked packet at offset %d:%d\n",
          (int)mLSID, (int)it->first, (int)(it->second->mOffset));fflush(stdout);*/
    }

    // And make sure we'll be able to ship the first buffer
    // immediately.
    if (!mQueuedBuffers.empty()) {
        StreamBufferPtr buffer = mQueuedBuffers.front();
        if (mTransmitWindowSize < buffer->mBufferLength)
            mTransmitWindowSize = buffer->mBufferLength;
    }

    mNumOutstandingBytes = 0;

    // If we're failing to get acks, we might be estimating the RTT
    // too low. To make sure it eventually updates, increase it.
    if (!mWaitingForAcks.empty()) {
      if (mStreamRTOMicroseconds < 20000000) {
        mStreamRTOMicroseconds *= 2;
      }
      // Also clear out the list of buffers waiting for acks since
      // they've timed out. They've been saved to the graveyard in
      // case we eventually get an ack back.
      mWaitingForAcks.clear();
    }
  }

  /* This function sends received data up to the application interface.
     mReceiveBufferMutex must be locked before calling this function. */
  void sendToApp(uint32 skipLength) {
      // Make sure we bail if we can't perform the callback since
      // checking the ready range will clear that range from the
      // segment list.
      if (mReadCallback == NULL) return;

      ReceivedSegmentList::SegmentRange nextReadyRange = mReceivedSegments.readyRange(mNextByteExpected, skipLength);
      int64 readyBufferSize = ReceivedSegmentList::Length(nextReadyRange);
      if (ReceivedSegmentList::Length(nextReadyRange) == 0) return;


      uint8* recv_buf = receiveBuffer();
      mReadCallback(recv_buf, readyBufferSize);

      //now move the window forward...
      mLastContiguousByteReceived = mLastContiguousByteReceived + readyBufferSize;
      mNextByteExpected = mLastContiguousByteReceived + 1;

      memmove(recv_buf, recv_buf + readyBufferSize, MAX_RECEIVE_WINDOW - readyBufferSize);

      mReceiveWindowSize += readyBufferSize;
  }

  // Handle reception of data packets (INIT, REPLY, DATA). Return value
  // indicates if we actually stored the data (or already had it).
  bool receiveData( Sirikata::Protocol::SST::SSTChannelHeader* received_channel_msg,
      Sirikata::Protocol::SST::SSTStreamHeader* streamMsg,
      const void* buffer, uint64 offset, uint32 len )
  {
    const Time curTime = Timer::now();
    mLastReceiveTime = curTime;

    if (streamMsg->type() == streamMsg->REPLY) {
      mConnected = true;
      return true;
    }
    else { // INIT or DATA
        assert(streamMsg->type() == streamMsg->DATA || streamMsg->type() == streamMsg->INIT);
      boost::recursive_mutex::scoped_lock lock(mReceiveBufferMutex);

      int transmitWindowSize = pow(2.0, streamMsg->window()) - mNumOutstandingBytes;
      if (transmitWindowSize >= 0) {
        mTransmitWindowSize = transmitWindowSize;
      }
      else {
        mTransmitWindowSize = 0;
      }

      // If we generate an ack, we're acking this
      uint64 ack_seqno = received_channel_msg->transmit_sequence_number();

      /*std::cout << "offset=" << offset << " , mLastContiguousByteReceived=" << mLastContiguousByteReceived
        << " , mNextByteExpected=" << mNextByteExpected <<"\n";*/

      // We can get data after we've already received it and moved our window
      // past it (e.g. retries), so we have to handle this carefully. A simple
      // case is if we've already moved past this entire packet.
      if ((int64)(offset + len) <= mNextByteExpected)
          return true;

      int64 offsetInBuffer = offset - mNextByteExpected;
      // Currently we shouldn't be resegmenting data, so we should either get an
      // entire segment before mNextByteExpected (handled already), or an entire
      // one after. Because of this, we can currently assert at this point that
      // we shouldn't have negative offsets. If we ever end up sometimes
      // re-segmenting, then we might have segments that span this boundary and
      // have to do something more complicated.
      assert(offsetInBuffer >= 0);

      if ( len > 0 &&  (int64)(offset) == mNextByteExpected) {
        if (offsetInBuffer + len <= MAX_RECEIVE_WINDOW) {
	  mReceiveWindowSize -= len;

          assert(offsetInBuffer >= 0);
          assert(offsetInBuffer + len <= MAX_RECEIVE_WINDOW);
	  memcpy(receiveBuffer()+offsetInBuffer, buffer, len);
          assert((int64)offset >= mNextByteExpected);
          mReceivedSegments.insert(offset, len);

	  sendToApp(len);

	  //send back an ack.
          sendAckPacket(ack_seqno);
          return true;
	}
        else {
           //dont ack this packet.. its falling outside the receive window.
	  sendToApp(0);
          return false;
        }
      }
      else if (len > 0) {
	if ( (int64)(offset+len-1) <= (int64)mLastContiguousByteReceived) {
	  //printf("Acking packet which we had already received previously\n");
	  sendAckPacket(ack_seqno);
          return true;
	}
        else if (offsetInBuffer + len <= MAX_RECEIVE_WINDOW) {
	  assert (offsetInBuffer + len > 0);

          mReceiveWindowSize -= len;

          assert(offsetInBuffer >= 0);
          assert(offsetInBuffer + len <= MAX_RECEIVE_WINDOW);
   	  memcpy(receiveBuffer()+offsetInBuffer, buffer, len);
          assert((int64)offset >= mNextByteExpected);
          mReceivedSegments.insert(offset, len);

          sendAckPacket(ack_seqno);
          return true;
	}
	else {
	  //dont ack this packet.. its falling outside the receive window.
	  sendToApp(0);
          return false;
	}
      }
      else if (len == 0 && (int64)(offset) == mNextByteExpected) {
          // A zero length packet at the next expected offset. This is a keep
          // alive, which are just empty packets that we process to keep the
          // connection running. Send an ack so we don't end up with unacked
          // keep alive packets.
          sendAckPacket(ack_seqno);
          return true;
      }
    }
    // Anything else doesn't match what we're expecting, indicate failure
    return false;
  }

  // Handle reception of ACK packets.
  void receiveAck( Sirikata::Protocol::SST::SSTStreamHeader* streamMsg, uint64 channelSegmentID) {
    //handle any ACKS that might be included in the message...
    boost::mutex::scoped_lock lock(mQueueMutex);

    const Time curTime = Timer::now();
    StreamBufferPtr acked_buffer;
    // Whether we found it
    bool normal_ack = false;
    // First, check if we have the acked channel segment ID waiting
    // for an ack (i.e. waiting for the ack hasn't timed out). This
    // should be the normal case.
    ChannelToBufferMap::iterator to_buffer_it = mWaitingForAcks.find(channelSegmentID);
    if (to_buffer_it != mWaitingForAcks.end()) {
        acked_buffer = to_buffer_it->second;
        normal_ack = true;

        // Clear out references tracking this buffer for acks. First,
        // the obvious one here.
        mWaitingForAcks.erase(channelSegmentID);
        // Graveyard cleared below
    }
    else {
        // Otherwise, we check the graveyard to see if we had
        // retransmitted (or scheduled for retransmit) but now got the
        // the ack anyway.
        ChannelToBufferMap::iterator unacked_buffer_it = mUnackedGraveyard.find(channelSegmentID);
        if (unacked_buffer_it != mUnackedGraveyard.end()) {
            acked_buffer = unacked_buffer_it->second;

            // Clear references tracking this buffer.
            mUnackedGraveyard.erase(unacked_buffer_it);
            // In this case, we get rid of any entries actively
            // waiting for acks. We could safely do this no matter
            // where we found the ack, as we do with the graveyard
            // below, but we don't since scanning through the list of
            // waiting for acks is possibly very expensive, and in the
            // common case much more expensive than scanning through
            // the graveyard (which should usually be empty).
            for(typename ChannelToBufferMap::iterator it = mWaitingForAcks.begin(); it != mWaitingForAcks.end(); ++it) {
                if (*(it->second) == *acked_buffer) {
                    mWaitingForAcks.erase(it);
                    break;
                }
            }
        }
    }

    // If we found it, update state to reflect ack and deal with any
    // leftover references to the same buffer due to resends
    if (acked_buffer) {
        //printf("REMOVED ack packet at offset %d\n", (int)acked_buffer->mOffset);

        acked_buffer->mAckTime = curTime;
        // This gets updated regardless of the kind of ack because we
        // we're removing all trace of that buffer now and the bytes
        // are not really outstanding anymore since they've been
        // acked, even though we might still have a packet in flight
        // with those bytes. If we didn't do this when we ack from the
        // graveyard, we'd end up never clearing these bytes because
        // the newer channel ID becomes unackable (no more reference
        // to them).
        mNumOutstandingBytes -= acked_buffer->mBufferLength;

        // These operations only work during a normal ack because the
        // info is either inaccurate (transmit and ack times from
        // different packets) or outdated (advertised window from old,
        // delayed packet).
        if (normal_ack) {
            updateRTO(acked_buffer->mTransmitTime, acked_buffer->mAckTime);

            if ( (int) (pow(2.0, streamMsg->window()) - mNumOutstandingBytes) > 0 ) {
                assert( pow(2.0, streamMsg->window()) - mNumOutstandingBytes > 0);
                mTransmitWindowSize = pow(2.0, streamMsg->window()) - mNumOutstandingBytes;
            }
            else {
                mTransmitWindowSize = 0;
            }
        }

        // In either case, if we found the acked packet (we extracted
        // the buffer), we need to scan the graveyard for ones with
        // the same offset due to retransmits (even if we found the
        // acked one in the graveyard since we may have multiple
        // retransmits)
        std::vector <uint64> graveyardChannelIDs;
        for(typename ChannelToBufferMap::iterator graveyard_it = mUnackedGraveyard.begin(); graveyard_it != mUnackedGraveyard.end(); graveyard_it++) {
            if (*(graveyard_it->second) == *acked_buffer)
                graveyardChannelIDs.push_back(graveyard_it->first);
        }
        for (uint32 i=0; i< graveyardChannelIDs.size(); i++)
            mUnackedGraveyard.erase(graveyardChannelIDs[i]);
    }

    // If we acked messages, we've cleared space in the transmit
    // buffer (the receiver cleared something out of its receive
    // buffer). We can send more data, so schedule servicing if we
    // have anything queued.
    if (acked_buffer && !mQueuedBuffers.empty())
        scheduleStreamService();
  }

  LSID getLSID() {
    return mLSID;
  }

  LSID getRemoteLSID() {
    return mRemoteLSID;
  }

  void updateRTO(Time sampleStartTime, Time sampleEndTime) {


    if (sampleStartTime > sampleEndTime ) {
      SST_LOG(insane, "Bad sample\n");
      return;
    }

    if (mFirstRTO) {
      mStreamRTOMicroseconds = (sampleEndTime - sampleStartTime).toMicroseconds() ;
      mFirstRTO = false;
    }
    else {

      mStreamRTOMicroseconds = FL_ALPHA * mStreamRTOMicroseconds +
	(1.0-FL_ALPHA) * (sampleEndTime - sampleStartTime).toMicroseconds();
    }

  }

  void sendInitPacket(void* data, uint32 len) {
    Sirikata::Protocol::SST::SSTStreamHeader sstMsg;
    sstMsg.set_lsid( mLSID );
    sstMsg.set_type(sstMsg.INIT);
    sstMsg.set_flags(0);
    sstMsg.set_window( log((double)mReceiveWindowSize)/log(2.0)  );
    sstMsg.set_src_port(mLocalPort);
    sstMsg.set_dest_port(mRemotePort);

    sstMsg.set_psid(mParentLSID);

    sstMsg.set_bsn(0);

    sstMsg.set_payload(data, len);

    std::string buffer = serializePBJMessage(sstMsg);


    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();

    if (!conn) return;

    conn->sendDataWithAutoAck( buffer.data(), buffer.size(), false );

    scheduleStreamService(Duration::microseconds(pow(2.0,mNumInitRetransmissions)*mStreamRTOMicroseconds));
  }

  void sendAckPacket(uint64 ack_seqno) {
    Sirikata::Protocol::SST::SSTStreamHeader sstMsg;
    sstMsg.set_lsid( mLSID );
    sstMsg.set_type(sstMsg.ACK);
    sstMsg.set_flags(0);
    sstMsg.set_window( log((double)mReceiveWindowSize)/log(2.0)  );
    sstMsg.set_src_port(mLocalPort);
    sstMsg.set_dest_port(mRemotePort);
    std::string buffer = serializePBJMessage(sstMsg);

    //printf("Sending Ack packet with window %d\n", (int)sstMsg.window());

    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    assert(conn);
    conn->sendData(buffer.data(), buffer.size(), true, ack_seqno);
  }

  uint64 sendDataPacket(const void* data, uint32 len, uint64 offset) {
    Sirikata::Protocol::SST::SSTStreamHeader sstMsg;
    sstMsg.set_lsid( mLSID );
    sstMsg.set_type(sstMsg.DATA);
    sstMsg.set_flags(0);
    sstMsg.set_window( log((double)mReceiveWindowSize)/log(2.0)  );
    sstMsg.set_src_port(mLocalPort);
    sstMsg.set_dest_port(mRemotePort);

    sstMsg.set_bsn(offset);

    sstMsg.set_payload(data, len);

    std::string buffer = serializePBJMessage(sstMsg);

    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    assert(conn);
    return conn->sendDataWithAutoAck(  buffer.data(), buffer.size(), false);
  }

  // Reply packets should be in repsonse to other side initiating connection, so
  // we should have a seqno to ack for the INIT packet
  void sendReplyPacket(void* data, uint32 len, LSID remoteLSID, uint64 ack_seqno) {
    Sirikata::Protocol::SST::SSTStreamHeader sstMsg;
    sstMsg.set_lsid( mLSID );
    sstMsg.set_type(sstMsg.REPLY);
    sstMsg.set_flags(0);
    sstMsg.set_window( log((double)mReceiveWindowSize)/log(2.0)  );
    sstMsg.set_src_port(mLocalPort);
    sstMsg.set_dest_port(mRemotePort);

    sstMsg.set_rsid(remoteLSID);
    sstMsg.set_bsn(0);

    sstMsg.set_payload(data, len);
    std::string buffer = serializePBJMessage(sstMsg);

    std::tr1::shared_ptr<Connection<EndPointType> > conn = mConnection.lock();
    assert(conn);
    conn->sendData(  buffer.data(), buffer.size(), false, ack_seqno);
  }

  uint8 mState;

  uint32 mLocalPort;
  uint32 mRemotePort;

  uint64 mNumBytesSent;

  LSID mParentLSID;

  //weak_ptr to avoid circular dependency between Connection and Stream classes
  std::tr1::weak_ptr<Connection<EndPointType> > mConnection;
  const Context* mContext;

  // Map from channel segment ID to the actual buffer of data for data
  // currently in flight. If we resend, this gets cleared, so it is
  // only tracking the *latest* channel ID for the buffer.
  typedef std::map<uint64, std::tr1::shared_ptr<StreamBuffer> >  ChannelToBufferMap;
  ChannelToBufferMap mWaitingForAcks;
  // Meanwhile, this tracks channel -> buffer for packets that missed
  // their ack deadline. A buffer can be in both mWaitingForAcks and
  // mUnackedGraveyard at the same time (and in the graveyard multiple
  // times) due to resends. The graveyard is used to take advantage of
  // slow acks, allowing us to avoid resending some data if an ack is
  // just received late. This doesn't cost us much: the buffer will
  // have been held onto anyway, the map remains small as long as we
  // don't have a ton of lost packets, and we only need to do
  // something relatively expensive (scan through the entire map) when
  // we handle the hopefully uncommon case of a slow ack.
  //
  // Using this approach makes the ack unusable for RTT estimation and
  // transmit window updates, but this should be an unusual case
  // anyway. We hold onto the buffer so we can mark the actual buffer
  // as acked so that if it got requeued for resending but hasn't been
  // sent yet, it won't be sent again when it does reach the front of
  // the send queue.
  ChannelToBufferMap mUnackedGraveyard;

  std::deque< std::tr1::shared_ptr<StreamBuffer> > mQueuedBuffers;
  uint32 mCurrentQueueLength;

  USID mUSID;
  LSID mLSID;
  LSID mRemoteLSID;

  uint16 MAX_PAYLOAD_SIZE;
  uint32 MAX_QUEUE_LENGTH;
  uint32 MAX_RECEIVE_WINDOW;

  boost::mutex mQueueMutex;

  bool mFirstRTO;
  int64 mStreamRTOMicroseconds;
  float FL_ALPHA;


  uint32 mTransmitWindowSize;
  uint32 mReceiveWindowSize;
  uint32 mNumOutstandingBytes;

  int64 mNextByteExpected;
  int64 mLastContiguousByteReceived;
  Time mLastSendTime;
  Time mLastReceiveTime;

  uint8* mReceiveBuffer;
  ReceivedSegmentList mReceivedSegments;
  boost::recursive_mutex mReceiveBufferMutex;

  ReadCallback mReadCallback;
  StreamReturnCallbackFunction mStreamReturnCallback;

  friend class Connection<EndPointType>;

  /* Variables required for the initial connection */
  bool mConnected;
  uint8* mInitialData;
  uint16 mInitialDataLength;
  uint8 mNumInitRetransmissions;
  uint8 MAX_INIT_RETRANSMISSIONS;

  ConnectionVariables<EndPointType>* mSSTConnVars;

  std::tr1::weak_ptr<Stream<EndPointType> > mWeakThis;

  /** Store the endpoints here to avoid talking to mConnection. It's ok
   to do this because the endpoints never change for an SST Stream.**/
  EndPoint <EndPointType> mLocalEndPoint;
  EndPoint <EndPointType> mRemoteEndPoint;

  // We need to transmit keep alives so the stream stays open even if
  // we don't transmit for a long time. It can still be closed by the
  // underlying connection being removed.
  Network::IOTimerPtr mKeepAliveTimer;

  // We can schedule servicing from multiple threads, so we need to
  // lock protect this data.
  boost::mutex mSchedulingMutex;
  // One timer to track servicing. Only one servicing should be scheduled at any
  // time. Scheduling servicing only does something if nothing is scheduled yet
  // or if the time it's scheduled for is too late, in which case we update the
  // timer.
  Network::IOTimerPtr mServiceTimer;
  // Sometimes we do servicing directly, i.e. just a post. We need to
  // track this to make sure we don't double-schedule because the
  // timer expiry won't be meaningful in that case
  bool mIsAsyncServicing;
  // We need to keep a strong reference to ourselves while we're waiting for
  // servicing. We'll also use this as an indicator of whether servicing is
  // currently scheduled.
  StreamPtr mServiceStrongRef;
  ConnectionPtr mServiceStrongConnRef;
  // Schedules servicing to occur after the given amount of time.
  void scheduleStreamService() {
      scheduleStreamService(Duration::zero());
  }
  void scheduleStreamService(const Duration& after) {
      std::tr1::shared_ptr<Connection<EndPointType> > conn =  mConnection.lock();
      if (!conn) return;

      boost::mutex::scoped_lock lock(mSchedulingMutex);

      bool needs_scheduling = false;
      if (!mServiceStrongRef) {
          needs_scheduling = true;
      }
      else if(!mIsAsyncServicing && mServiceTimer->expiresFromNow() > after) {
          needs_scheduling = true;
          // No need to check success because we're using a strand and we can
          // only get here if timer->expiresFromNow() is positive.
          mServiceTimer->cancel();
      }

      if (needs_scheduling) {
          mServiceStrongRef = mWeakThis.lock();
          assert(mServiceStrongRef);
          mServiceStrongConnRef = conn;
          if (after == Duration::zero()) {
              mIsAsyncServicing = true;
              getContext()->mainStrand->post(
                  std::tr1::bind(&Stream<EndPointType>::serviceStreamNoReturn, this),
                  "Stream<EndPointType>::serviceStreamNoReturn"
              );
          }
          else {
              mServiceTimer->wait(after);
          }
      }
  }
  std::pair<StreamPtr, ConnectionPtr> startServicing() {
      // Need to do some basic bookkeeping that makes sure that a) we
      // have a proper shared_ptr to ourselves, b) we'll hold onto it
      // for the duration of this call and c) we clear out our
      // scheduled servicing time so calls to schedule servicing will
      // work
      boost::mutex::scoped_lock lock(mSchedulingMutex);
      StreamPtr strm = mServiceStrongRef;
      ConnectionPtr conn = mServiceStrongConnRef;
      assert(strm);
      assert(conn);
      // Just clearing the strong ref is enough to let someone else schedule
      // servicing.
      mServiceStrongRef.reset();
      mServiceStrongConnRef.reset();
      // Make sure if this was a direct async post that we don't
      // still have it marked as such.
      mIsAsyncServicing = false;

      return std::make_pair(strm, conn);
  }
};


/**
 Manages SST Connections. All calls creating new top-level streams, listening on endpoints,
 or creating the underlying datagram layer go through here. This class maintains the data structures
 needed by every new SST Stream or Connection.

 This class is only instantiated once per process (usually in main()) and is then
 accessible through SpaceContext and ObjectHostContext.
 */
template <class EndPointType>
class ConnectionManager : public Service {
public:
  typedef std::tr1::shared_ptr<BaseDatagramLayer<EndPointType> > BaseDatagramLayerPtr;

    typedef CallbackTypes<EndPointType> CBTypes;
    typedef typename CBTypes::StreamReturnCallbackFunction StreamReturnCallbackFunction;

  virtual void start() {
  }

  virtual void stop() {
      // Give the connections a chance to stop, performing specific
      // cleanup operations and notifying streams
      Connection<EndPointType>::stopConnections(&mSSTConnVars);
  }

  ~ConnectionManager() {
    Connection<EndPointType>::closeConnections(&mSSTConnVars);
  }

  bool connectStream(EndPoint <EndPointType> localEndPoint,
                     EndPoint <EndPointType> remoteEndPoint,
                     StreamReturnCallbackFunction cb)
  {
    return Stream<EndPointType>::connectStream(&mSSTConnVars, localEndPoint, remoteEndPoint, cb);
  }

    // The BaseDatagramLayer is really where the interaction with the underlying
    // system happens, and different underlying protocols may require different
    // parameters. These need to be instantiated by the client code anyway (to
    // generate the interface), so we provide some templatized versions to allow
    // a variable number of arguments.
    template<typename A1>
    BaseDatagramLayerPtr createDatagramLayer(EndPointType endPoint, Context* ctx, A1 a1) {
        return BaseDatagramLayer<EndPointType>::createDatagramLayer(&mSSTConnVars, endPoint, ctx, a1);
    }
    template<typename A1, typename A2>
    BaseDatagramLayerPtr createDatagramLayer(EndPointType endPoint, Context* ctx, A1 a1, A2 a2) {
        return BaseDatagramLayer<EndPointType>::createDatagramLayer(&mSSTConnVars, endPoint, ctx, a1, a2);
    }
    template<typename A1, typename A2, typename A3>
    BaseDatagramLayerPtr createDatagramLayer(EndPointType endPoint, Context* ctx, A1 a1, A2 a2, A3 a3) {
        return BaseDatagramLayer<EndPointType>::createDatagramLayer(&mSSTConnVars, endPoint, ctx, a1, a2, a3);
    }

  BaseDatagramLayerPtr getDatagramLayer(EndPointType endPoint) {
    return mSSTConnVars.getDatagramLayer(endPoint);
  }

  bool listen(StreamReturnCallbackFunction cb, EndPoint <EndPointType> listeningEndPoint) {
    return Stream<EndPointType>::listen(&mSSTConnVars, cb, listeningEndPoint);
  }

  bool unlisten( EndPoint <EndPointType> listeningEndPoint) {
    return Stream<EndPointType>::unlisten(&mSSTConnVars, listeningEndPoint);
  }

  //Storage class for SST's global variables.
  ConnectionVariables<EndPointType> mSSTConnVars;
};



} // namespace SST
} // namespace Sirikata

#endif
