/*  Sirikata Network Utilities
 *  Stream.hpp
 *
 *  Copyright (c) 2009, Daniel Reiter Horn
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

#ifndef SIRIKATA_TCPStreamListener_HPP__
#define SIRIKATA_TCPStreamListener_HPP__
#include "StreamListener.hpp"
namespace Sirikata { namespace Network {
/**
 * Begins a new stream based on a TCPSocket connection acception with the following substream callback for stream creation
 * Only creates the stream if the handshake is complete and it has all the resources (udp, tcp sockets, etc) necessary at the time
 * Defined in TCPStream.cpp
 */
void beginNewStream(TCPSocket *socket,const Stream::SubstreamCallback&);
/**
 * This class waits on a service and listens for incoming connections
 * It calls the callback whenever such connections are encountered
 */
class TCPStreamListener:public StreamListener{

public:
    TCPStreamListener(IOService&);
    ///subclasses will expose these methods with similar arguments + protocol specific args
    virtual bool listen(
        const Address&addy,
        const Stream::SubstreamCallback&newStreamCallback);
    ///returns the name of the computer followed by a colon and then the service being listened on
    virtual String listenAddressName()const;
    ///returns the name of the computer followed by a colon and then the service being listened on
    virtual Address listenAddress()const;
    ///stops listening
    virtual void close();   
    virtual ~TCPStreamListener();
    IOService * mIOService;
    TCPListener *mTCPAcceptor;
};
} }
#endif
