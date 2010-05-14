/*  Sirikata liboh -- Object Host
 *  HostedObject.cpp
 *
 *  Copyright (c) 2009, Patrick Reiter Horn
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

#include <oh/Platform.hpp>
#include "proxyobject/ProxyMeshObject.hpp"
#include "proxyobject/ProxyLightObject.hpp"
#include "proxyobject/ProxyWebViewObject.hpp"
#include "proxyobject/ProxyCameraObject.hpp"
#include "proxyobject/LightInfo.hpp"
#include <ObjectHost_Sirikata.pbj.hpp>
#include <ObjectHost_Subscription.pbj.hpp>
#include <ObjectHost_Persistence.pbj.hpp>
#include <task/WorkQueue.hpp>
#include "util/RoutableMessage.hpp"
#include "util/KnownServices.hpp"
#include "persistence/PersistenceSentMessage.hpp"
#include "network/Stream.hpp"
#include "util/SpaceObjectReference.hpp"
#include "oh/SpaceConnection.hpp"
#include "oh/TopLevelSpaceConnection.hpp"
#include "oh/HostedObject.hpp"
#include "util/SentMessage.hpp"
#include "oh/ObjectHost.hpp"

#include "oh/ObjectScriptManager.hpp"
#include "options/Options.hpp"
#include "oh/ObjectScript.hpp"
#include "oh/ObjectScriptManagerFactory.hpp"
#include <util/KnownServices.hpp>
#include "util/ThreadId.hpp"
#include "util/PluginManager.hpp"

#include <core/odp/Exceptions.hpp>

namespace Sirikata {

typedef SentMessageBody<RoutableMessageBody> RPCMessage;

OptionValue *defaultTTL;

InitializeGlobalOptions hostedobject_props("",
    defaultTTL=new OptionValue("defaultTTL",".1",OptionValueType<Duration>(),"Default TTL for HostedObject properties"),
    NULL
);

class HostedObject::PerSpaceData {
public:

    HostedObject* parent;
    SpaceID space;
    ObjectReference object;
    SpaceConnection mSpaceConnection;
    ProxyObjectPtr mProxyObject;
    ProxyObject::Extrapolator mUpdatedLocation;

    ODP::Port* rpcPort;
    ODP::Port* persistencePort;

    QueryTracker* tracker;

    void locationWasReset(Time timestamp, Location loc) {
        loc.setVelocity(Vector3f::nil());
        loc.setAngularSpeed(0);
        mUpdatedLocation.resetValue(timestamp, loc);
    }
    void locationWasSet(const Protocol::ObjLoc &msg) {
        Time timestamp = msg.timestamp();
        Location loc = mUpdatedLocation.extrapolate(timestamp);
        ProxyObject::updateLocationWithObjLoc(loc, msg);
        loc.setVelocity(Vector3f::nil());
        loc.setAngularSpeed(0);
        mUpdatedLocation.updateValue(timestamp, loc);
    }

    void updateLocation(HostedObject *ho) {
        if (!mProxyObject) {
            return;
        }
        SpaceID space = mProxyObject->getObjectReference().space();
        Time now = Time::now(ho->getSpaceTimeOffset(space));
        Location realLocation = mProxyObject->globalLocation(now);
        if (mUpdatedLocation.needsUpdate(now, realLocation)) {
            Protocol::ObjLoc toSet;
            toSet.set_position(realLocation.getPosition());
            toSet.set_velocity(realLocation.getVelocity());
            RoutableMessageBody body;
            toSet.SerializeToString(body.add_message("ObjLoc"));
            RoutableMessageHeader header;
            header.set_destination_port(Services::LOC);
            header.set_destination_object(ObjectReference::spaceServiceID());
            header.set_destination_space(space);
            std::string bodyStr;
            body.SerializeToString(&bodyStr);
            // Avoids waiting a loop.
            ho->sendViaSpace(header, MemoryReference(bodyStr));

            locationWasSet(toSet);
        }
    }

    typedef std::map<uint32, std::set<ObjectReference> > ProxQueryMap;
    ProxQueryMap mProxQueryMap; ///< indexed by ProxCall::query_id()

    PerSpaceData(HostedObject* _parent, const SpaceID& _space,
        const std::tr1::shared_ptr<TopLevelSpaceConnection>&topLevel, Network::Stream*stream)
     : parent(_parent),
       space(_space),
       mSpaceConnection(topLevel,stream),
       mUpdatedLocation(
            Duration::seconds(.1),
            TemporalValue<Location>::Time::null(),
            Location(Vector3d(0,0,0),Quaternion(Quaternion::identity()),
                     Vector3f(0,0,0),Vector3f(0,1,0),0),
            ProxyObject::UpdateNeeded()),
       rpcPort(NULL),
       persistencePort(NULL),
       tracker(NULL)
    {
    }

    void setObject(const ObjectReference& obj) {
        object = obj;
    }

    void initializeAs(ProxyObjectPtr proxyobj) {
        assert(proxyobj->getObjectReference().object() == object);

        mProxyObject = proxyobj;
        rpcPort = parent->bindODPPort(space, ODP::PortID((uint32)Services::RPC));
        persistencePort = parent->bindODPPort(space, ODP::PortID((uint32)Services::PERSISTENCE));

        // Use any port for tracker
        tracker = new QueryTracker(parent->bindODPPort(space), parent->mObjectHost->getSpaceIO());
        tracker->forwardMessagesTo(&parent->mSendService);
    }

    void destroy(QueryTracker *tracker) const {
        delete rpcPort;
        delete persistencePort;

        if (mProxyObject) {
            mSpaceConnection.getTopLevelStream()->
                destroyObject(mProxyObject, tracker);
        }
        for (PerSpaceData::ProxQueryMap::const_iterator qiter = mProxQueryMap.begin();
             qiter != mProxQueryMap.end();
             ++qiter) {
            for (std::set<ObjectReference>::const_iterator oriter = qiter->second.begin();
                 oriter != qiter->second.end();
                 ++oriter)
            {
                ProxyObjectPtr which=mSpaceConnection.getTopLevelStream()->
                    getProxyObject(SpaceObjectReference(
                                       mSpaceConnection.getTopLevelStream()->id(),
                                       *oriter));
                if (which) {
                    mSpaceConnection.getTopLevelStream()->
                        destroyObject(which, tracker);
                }
            }
        }

        if (tracker) {
            tracker->endForwardingMessagesTo(&parent->mSendService);
            delete tracker;
        }
    }
};



HostedObject::HostedObject(ObjectHost*parent, const UUID &objectName)
    : mInternalObjectReference(objectName)
{
    mSpaceData = new SpaceDataMap;
    mNextSubscriptionID = 0;
    mObjectHost=parent;
    mObjectScript=NULL;
    mSendService.ho = this;
    mReceiveService.ho = this;

    mDelegateODPService = new ODP::DelegateService(
        std::tr1::bind(
            &HostedObject::createDelegateODPPort, this,
            _1, _2, _3
        )
    );

    mDefaultTracker = NULL;
}

HostedObject::~HostedObject() {
    if (mDefaultTracker != NULL)
        delete mDefaultTracker;

    destroy();
    delete mSpaceData;
}

void HostedObject::destroy() {
    if (mObjectScript) {
        delete mObjectScript;
        mObjectScript=NULL;
    }
    for (SpaceDataMap::const_iterator iter = mSpaceData->begin();
         iter != mSpaceData->end();
         ++iter) {
        iter->second.destroy(getTracker(iter->first));
    }
    mSpaceData->clear();
    mObjectHost->unregisterHostedObject(mInternalObjectReference);
    mProperties.clear();
}

struct HostedObject::PrivateCallbacks {

    static bool needsSubscription(const PropertyCacheValue &pcv) {
        return pcv.mTTL != Duration::zero();
    }

    static void initializeDatabaseCallback(
        const HostedObjectWPtr &weakThis,
        const SpaceID &spaceID,
        const HostedObjectPtr&spaceConnectionHint,
        Persistence::SentReadWriteSet *msg,
        const RoutableMessageHeader &lastHeader,
        Persistence::Protocol::Response::ReturnStatus errorCode)
    {
        HostedObjectPtr realThis=weakThis.lock();
        if (!realThis) {
            return;//deleted before database read
        }
        if (lastHeader.has_return_status() || errorCode) {
            SILOG(cppoh,error,"Database error recieving Loc and scripting info: "<<(int)lastHeader.return_status()<<": "<<(int)errorCode);
            delete msg;
            return; // unable to get starting position.
        }
        String scriptName;
        std::map<String,String> scriptParams;
        Location location(Vector3d::nil(),Quaternion::identity(),Vector3f::nil(),Vector3f(1,0,0),0);
        for (int i = 0; i < msg->body().reads_size(); i++) {
            String name = msg->body().reads(i).field_name();
            if (msg->body().reads(i).has_return_status() || !msg->body().reads(i).has_data()) {
                continue;
            }
            Duration ttl = msg->body().reads(i).has_ttl() ? msg->body().reads(i).ttl() : defaultTTL->as<Duration>();
            if (!name.empty() && name[0] != '_') {
                realThis->setProperty(name, ttl, msg->body().reads(i).data());
            }
            if (name == "Loc") {
                Protocol::ObjLoc loc;
                loc.ParseFromString(msg->body().reads(i).data());
                SILOG(cppoh,debug,"Creating object "<<ObjectReference(realThis->getUUID())
                      <<" at position "<<loc.position());
                if (loc.has_position()) {
                    location.setPosition(loc.position());
                }
                if (loc.has_orientation()) {
                    location.setOrientation(loc.orientation());
                }
                if (loc.has_velocity()) {
                    location.setVelocity(loc.velocity());
                }
                if (loc.has_rotational_axis()) {
                    location.setAxisOfRotation(loc.rotational_axis());
                }
                if (loc.has_angular_speed()) {
                    location.setAngularSpeed(loc.angular_speed());
                }
            }
            if (name == "_Script") {
                Protocol::StringProperty scrProp;
                scrProp.ParseFromString(msg->body().reads(i).data());
                scriptName = scrProp.value();
            }
            if (name == "_ScriptParams") {
                Protocol::StringMapProperty scrProp;
                scrProp.ParseFromString(msg->body().reads(i).data());
                int numkeys = scrProp.keys_size();
                {
                    int numvalues = scrProp.values_size();
                    if (numvalues < numkeys) {
                        numkeys = numvalues;
                    }
                }
                for (int i = 0; i < numkeys; i++) {
                    scriptParams[scrProp.keys(i)] = scrProp.values(i);
                }
            }
        }
        // Temporary Hack because we do not have access to the CDN here.
        BoundingSphere3f sphere(Vector3f::nil(),realThis->hasProperty("IsCamera")?1.0:1.0);
        realThis->connectToSpace(spaceID, spaceConnectionHint, location, sphere, realThis->getUUID());
        delete msg;
        if (!scriptName.empty()) {
            realThis->initializeScript(scriptName, scriptParams);
        }
    }

    static void receivedRoutableMessage(const HostedObjectWPtr&thus,const SpaceID&sid, const Network::Chunk&msgChunk, const Network::Stream::PauseReceiveCallback& pauseReceive) {
        HostedObjectPtr realThis (thus.lock());

        RoutableMessageHeader header;
        MemoryReference bodyData = header.ParseFromArray(&(msgChunk[0]),msgChunk.size());
        header.set_source_space(sid);
        header.set_destination_space(sid);

        if (!realThis) {
            SILOG(objecthost,error,"Received message for dead HostedObject. SpaceID = "<<sid<<"; DestObject = <deleted>");
            return;
        }

        {
            ProxyObjectPtr destinationObject = realThis->getProxy(header.source_space());
            if (destinationObject) {
                header.set_destination_object(destinationObject->getObjectReference().object());
            }
            if (!header.has_source_object()) {
                header.set_source_object(ObjectReference::spaceServiceID());
            }
        }

        realThis->processRoutableMessage(header, bodyData);
    }

    static void handlePersistenceResponse(
        HostedObject *realThis,
        const RoutableMessageHeader &origHeader,
        SentMessage *sent,
        const RoutableMessageHeader &header,
        MemoryReference bodyData)
    {
        std::auto_ptr<SentMessageBody<Persistence::Protocol::ReadWriteSet> > sentDestruct(static_cast<SentMessageBody<Persistence::Protocol::ReadWriteSet> *>(sent));
        SILOG(cppoh,debug,"Got some persistence back: stat = "<<(int)header.return_status());
        if (header.has_return_status()) {
            Persistence::Protocol::Response resp;
            for (int i = 0, respIndex=0; i < sentDestruct->body().reads_size(); i++, respIndex++) {
                Persistence::Protocol::IStorageElement field = resp.add_reads();
                if (sentDestruct->body().reads(i).has_index()) {
                    field.set_index(sentDestruct->body().reads(i).index());
                }
                if (sentDestruct->body().options() & Persistence::Protocol::ReadWriteSet::RETURN_READ_NAMES) {
                    field.set_field_name(sentDestruct->body().reads(i).field_name());
                }
                field.set_return_status(Persistence::Protocol::StorageElement::KEY_MISSING);
            }
            std::string errorData;
            resp.SerializeToString(&errorData);
            RoutableMessageHeader replyHeader = origHeader.createReply();
            realThis->sendViaSpace(replyHeader, MemoryReference(errorData));
        } else {
            Persistence::Protocol::Response resp;
            resp.ParseFromArray(bodyData.data(), bodyData.length());
            for (int i = 0, respIndex=0; i < resp.reads_size(); i++, respIndex++) {
                if (resp.reads(i).has_index()) {
                    respIndex=resp.reads(i).index();
                }
                const Persistence::Protocol::StorageElement &field = resp.reads(i);
                if (respIndex >= 0 && respIndex < sentDestruct->body().reads_size()) {
                    const Persistence::Protocol::StorageElement &sentField = sentDestruct->body().reads(i);
                    const std::string &fieldName = sentField.field_name();
                    Duration ttl = field.has_ttl() ? field.ttl() : defaultTTL->as<Duration>();
                    if (field.has_data()) {
                        realThis->setProperty(fieldName, ttl, field.data());
                        PropertyCacheValue &cachedProp = realThis->mProperties[fieldName];
                        if (!cachedProp.hasSubscriptionID() && needsSubscription(cachedProp)) {
                            cachedProp.setSubscriptionID(realThis->mNextSubscriptionID++);
                        }
                        if (cachedProp.hasSubscriptionID()) {
                            resp.reads(i).set_subscription_id(cachedProp.getSubscriptionID());
                        }
                    }
                }
                ++respIndex;
            }
            std::string newBodyData;
            resp.SerializeToString(&newBodyData);
            RoutableMessageHeader replyHeader = origHeader.createReply();
            realThis->sendViaSpace(replyHeader, MemoryReference(newBodyData));
        }
    }

    static void disconnectionEvent(const HostedObjectWPtr&weak_thus,const SpaceID&sid, const String&reason) {
        std::tr1::shared_ptr<HostedObject>thus=weak_thus.lock();
        if (thus) {
            SpaceDataMap::iterator where=thus->mSpaceData->find(sid);
            if (where!=thus->mSpaceData->end()) {
                where->second.destroy(thus->getTracker(sid));
                thus->mSpaceData->erase(where);//FIXME do we want to back this up to the database first?
            }
        }
    }

    static void connectionEvent(const HostedObjectWPtr&thus,
                                const SpaceID&sid,
                                Network::Stream::ConnectionStatus ce,
                                const String&reason) {
        if (ce!=Network::Stream::Connected) {
            disconnectionEvent(thus,sid,reason);
        }
    }
};


QueryTracker* HostedObject::getTracker(const SpaceID& space) {
    SpaceDataMap::iterator it = mSpaceData->find(space);
    if (it == mSpaceData->end()) return NULL;
    return it->second.tracker;
}

const QueryTracker* HostedObject::getTracker(const SpaceID& space) const {
    SpaceDataMap::const_iterator it = mSpaceData->find(space);
    if (it == mSpaceData->end()) return NULL;
    return it->second.tracker;
}


void HostedObject::handleRPCMessage(const RoutableMessageHeader &header, MemoryReference bodyData) {
    HostedObject *realThis=this;
    /// Parse message_names and message_arguments.

    // FIXME: Transitional. There are two ways this data could be
    // stored. The old way is directly in the bodyData.  The other is
    // in the payload section of bodyData.  Either way, we parse the
    // body, but in the former, we need to parse a *secondary*
    // RoutableMessageBody in the payload field.  Eventually this
    // should just be in the "header", i.e. the entire packet should
    // just be unified.
    RoutableMessageBody msg;
    RoutableMessageBody outer_msg;
    outer_msg.ParseFromArray(bodyData.data(), bodyData.length());
    if (outer_msg.has_payload()) {
        assert( outer_msg.message_size() == 0 );
        msg.ParseFromString(outer_msg.payload());
    }
    else {
        msg = outer_msg;
    }

    int numNames = msg.message_size();
    if (numNames <= 0) {
        // Invalid message!
        RoutableMessageHeader replyHeader = header.createReply();
        replyHeader.set_return_status(RoutableMessageHeader::PROTOCOL_ERROR);
        sendViaSpace(replyHeader, MemoryReference::null());
        return;
    }

    RoutableMessageBody responseMessage;
    for (int i = 0; i < numNames; ++i) {
        std::string name = msg.message_names(i);
        MemoryReference body(msg.message_arguments(i));

        if (header.has_id()) {
            std::string response;
            /// Pass response parameter if we expect a response.
            realThis->processRPC(header, name, body, &response);
            responseMessage.add_message_reply(response);
        } else {
            /// Return value not needed.
            realThis->processRPC(header, name, body, NULL);
        }
    }

    if (header.has_id()) {
        std::string serializedResponse;
        responseMessage.SerializeToString(&serializedResponse);
        RoutableMessageHeader replyHeader = header.createReply();
        realThis->sendViaSpace(replyHeader, MemoryReference(serializedResponse));
    }
}


void HostedObject::handlePersistenceMessage(const RoutableMessageHeader &header, MemoryReference bodyData) {
        using namespace Persistence::Protocol;
        HostedObject *realThis=this;
        ReadWriteSet rws;
        rws.ParseFromArray(bodyData.data(), bodyData.length());

        Response immedResponse;
        int immedIndex = 0;

        SpaceID space = header.destination_space();
        QueryTracker* space_query_tracker = getTracker(space);

        SentMessageBody<ReadWriteSet> *persistenceMsg = new SentMessageBody<ReadWriteSet>(space_query_tracker,std::tr1::bind(&PrivateCallbacks::handlePersistenceResponse, realThis, header, _1, _2, _3));
        int outIndex = 0;
        ReadWriteSet &outMessage = persistenceMsg->body();
        if (rws.has_options()) {
            outMessage.set_options(rws.options());
        }
        SILOG(cppoh,debug,"Got a Persistence message: reads size = "<<rws.reads_size()<<
              " writes size = "<<rws.writes_size());

        for (int i = 0, rwsIndex=0 ; i < rws.reads_size(); i++, rwsIndex++) {
            if (rws.reads(i).has_index()) {
                rwsIndex = rws.reads(i).index();
            }
            std::string name;
            if (rws.reads(i).has_field_name()) {
                name = rws.reads(i).field_name();
            }
            bool fail = false;
            if (name.empty() || name[0] == '_') {
                SILOG(cppoh,debug,"Invalid GetProp: "<<name);
                fail = true;
            } else {
                if (realThis->hasProperty(name)) {
                    PropertyCacheValue &cachedProp = realThis->mProperties[name];
                    // Cached property--respond immediately.
                    SILOG(cppoh,debug,"Cached GetProp: "<<name<<" = "<<realThis->getProperty(name));
                    IStorageElement el = immedResponse.add_reads();
                    if (immedIndex != rwsIndex) {
                        el.set_index(rwsIndex);
                    }
                    immedIndex = rwsIndex+1;
                    if (rws.options() & ReadWriteSet::RETURN_READ_NAMES) {
                        el.set_field_name(rws.reads(i).field_name());
                    }
                    el.set_ttl(cachedProp.mTTL);
                    el.set_data(cachedProp.mData);
                    if (!cachedProp.hasSubscriptionID() && PrivateCallbacks::needsSubscription(cachedProp)) {
                        cachedProp.setSubscriptionID(mNextSubscriptionID++);
                    }
                    if (cachedProp.hasSubscriptionID()) {
                        el.set_subscription_id(cachedProp.getSubscriptionID());
                    }
                } else {
                    SILOG(cppoh,debug,"Forward GetProp: "<<name<<" to Persistence");
                    IStorageElement el = outMessage.add_reads();
                    if (outIndex != rwsIndex) {
                        el.set_index(rwsIndex);
                    }
                    outIndex = rwsIndex+1;
                    el.set_field_name(rws.reads(i).field_name());
                    el.set_object_uuid(realThis->getUUID());
                }
            }
            if (fail) {
                IStorageElement el = immedResponse.add_reads();
                if (immedIndex != rwsIndex) {
                    el.set_index(rwsIndex);
                }
                immedIndex = rwsIndex+1;
                if (rws.options() & ReadWriteSet::RETURN_READ_NAMES) {
                    el.set_field_name(rws.reads(i).field_name());
                }
                el.set_return_status(StorageElement::KEY_MISSING);
            }
        }
        outIndex = 0;
        for (int i = 0, rwsIndex=0 ; i < rws.writes_size(); i++, rwsIndex++) {
            if (rws.writes(i).has_index()) {
                rwsIndex = rws.writes(i).index();
            }
            std::string name;
            if (rws.writes(i).has_field_name()) {
                name = rws.writes(i).field_name();
            }
            bool fail = false;
            if (name.empty() || name[0] == '_') {
                SILOG(cppoh,debug,"Invalid SetProp: "<<name);
                fail = true;
            } else {
                if (rws.writes(i).has_data()) {
                    Duration ttl = rws.writes(i).has_ttl() ? rws.writes(i).ttl() : defaultTTL->as<Duration>();
                    realThis->setProperty(name, ttl, rws.writes(i).data());
                    PropertyCacheValue &cachedProp = mProperties[name];
                    if (cachedProp.hasSubscriptionID()) {
                        SpaceDataMap::const_iterator spaceiter = mSpaceData->begin();
                        for (;spaceiter != mSpaceData->end(); ++spaceiter) {
                            int subID = cachedProp.getSubscriptionID();
                            Protocol::Broadcast subMsg;
                            std::string subStr;
                            subMsg.set_broadcast_name(subID);
                            subMsg.set_data(cachedProp.mData);
                            subMsg.SerializeToString(&subStr);
                            RoutableMessageHeader header;
                            header.set_destination_port(Services::BROADCAST);
                            header.set_destination_space(spaceiter->first);
                            header.set_destination_object(ObjectReference::spaceServiceID());
                            sendViaSpace(header, MemoryReference(subStr.data(), subStr.length()));
                        }
                    }
                    SpaceDataMap::iterator iter;
                    for (iter = realThis->mSpaceData->begin();
                         iter != realThis->mSpaceData->end();
                         ++iter) {
                        realThis->receivedPropertyUpdate(iter->second.mProxyObject, name, rws.writes(i).data());
                    }
                } else {
                    if (name != "LightInfo" && name != "MeshURI" && name != "IsCamera" && name != "WebViewURL") {
                        // changing the type of this object has to wait until we reload from database.
                        realThis->unsetCachedPropertyAndSubscription(name);
                    }
                }
                SILOG(cppoh,debug,"Forward SetProp: "<<name<<" to Persistence");
                IStorageElement el = outMessage.add_writes();
                if (outIndex != rwsIndex) {
                    el.set_index(rwsIndex);
                }
                outIndex = rwsIndex+1;
                el.set_field_name(rws.writes(i).field_name());
                if (rws.writes(i).has_data()) {
                    el.set_data(rws.writes(i).data());
                }
                el.set_object_uuid(realThis->getUUID());
            }
            // what to do if a write fails?
        }

        if (immedResponse.reads_size()) {
            SILOG(cppoh,debug,"ImmedResponse: "<<immedResponse.reads_size());
            std::string respStr;
            immedResponse.SerializeToString(&respStr);
            RoutableMessageHeader replyHeader = header.createReply();
            realThis->sendViaSpace(replyHeader, MemoryReference(respStr));
        }
        if (outMessage.reads_size() || outMessage.writes_size()) {
            SILOG(cppoh,debug,"ForwardToPersistence: "<<outMessage.reads_size()<<
                  " reads and "<<outMessage.writes_size()<<"writes");
            persistenceMsg->header().set_destination_space(SpaceID::null());
            persistenceMsg->header().set_destination_object(ObjectReference::spaceServiceID());
            persistenceMsg->header().set_destination_port(Services::PERSISTENCE);

            persistenceMsg->serializeSend();
        } else {
            delete persistenceMsg;
        }
    }



HostedObject::PerSpaceData& HostedObject::cloneTopLevelStream(const SpaceID&sid,const std::tr1::shared_ptr<TopLevelSpaceConnection>&tls) {
    using std::tr1::placeholders::_1;
    using std::tr1::placeholders::_2;
    mSpaces.insert(sid);
    SpaceDataMap::iterator iter = mSpaceData->insert(
        SpaceDataMap::value_type(
            sid,
            PerSpaceData(this,
                sid,
                tls,
                         tls->topLevelStream()->clone(
                             std::tr1::bind(&PrivateCallbacks::connectionEvent,
                                            getWeakPtr(),
                                            sid,
                                            _1,
                                            _2),
                             std::tr1::bind(&PrivateCallbacks::receivedRoutableMessage,
                                            getWeakPtr(),
                                            sid,
                                 _1, _2),
                             &Network::Stream::ignoreReadySendCallback)
            ))).first;
    return iter->second;
}

static String nullProperty;
bool HostedObject::hasProperty(const String &propName) const {
    PropertyMap::const_iterator iter = mProperties.find(propName);
    return (iter != mProperties.end());
}
const String &HostedObject::getProperty(const String &propName) const {
    PropertyMap::const_iterator iter = mProperties.find(propName);
    if (iter != mProperties.end()) {
        return (*iter).second.mData;
    }
    return nullProperty;
}
String *HostedObject::propertyPtr(const String &propName, Duration ttl) {
    PropertyCacheValue &pcv = mProperties[propName];
    pcv.mTTL = ttl;
    return &(pcv.mData);
}
void HostedObject::setProperty(const String &propName, Duration ttl, const String &encodedValue) {
    PropertyMap::iterator iter = mProperties.find(propName);
    if (iter == mProperties.end()) {
        iter = mProperties.insert(PropertyMap::value_type(propName,
            PropertyCacheValue(encodedValue, ttl))).first;
    }
}
void HostedObject::unsetCachedPropertyAndSubscription(const String &propName) {
    PropertyMap::iterator iter = mProperties.find(propName);
    if (iter != mProperties.end()) {
        if (iter->second.hasSubscriptionID()) {
            SpaceDataMap::const_iterator spaceiter = mSpaceData->begin();
            for (;spaceiter != mSpaceData->end(); ++spaceiter) {
                int subID = iter->second.getSubscriptionID();
                Protocol::Broadcast subMsg;
                std::string subStr;
                subMsg.set_broadcast_name(subID);
                subMsg.SerializeToString(&subStr);
                RoutableMessageHeader header;
                header.set_destination_port(Services::BROADCAST);
                header.set_destination_space(spaceiter->first);
                header.set_destination_object(ObjectReference::spaceServiceID());
                sendViaSpace(header, MemoryReference(subStr.data(), subStr.length()));
            }
        }
        mProperties.erase(iter);
    }
}


static ProxyObjectPtr nullPtr;
const ProxyObjectPtr &HostedObject::getProxy(const SpaceID &space) const {
    SpaceDataMap::const_iterator iter = mSpaceData->find(space);
    if (iter == mSpaceData->end()) {
        return nullPtr;
    }
    return iter->second.mProxyObject;
}


using Sirikata::Protocol::NewObj;
using Sirikata::Protocol::IObjLoc;

void HostedObject::sendNewObj(
    const Location&startingLocation,
    const BoundingSphere3f &meshBounds,
    const SpaceID&spaceID,
    const UUID&object_uuid_evidence)
{

    RoutableMessageHeader messageHeader;
    messageHeader.set_destination_object(ObjectReference::spaceServiceID());
    messageHeader.set_destination_space(spaceID);
    messageHeader.set_destination_port(Services::REGISTRATION);
    NewObj newObj;
    newObj.set_object_uuid_evidence(object_uuid_evidence);
    newObj.set_bounding_sphere(meshBounds);
    IObjLoc loc = newObj.mutable_requested_object_loc();
    loc.set_timestamp(Time::now(getSpaceTimeOffset(spaceID)));
    loc.set_position(startingLocation.getPosition());
    loc.set_orientation(startingLocation.getOrientation());
    loc.set_velocity(startingLocation.getVelocity());
    loc.set_rotational_axis(startingLocation.getAxisOfRotation());
    loc.set_angular_speed(startingLocation.getAngularSpeed());

    RoutableMessageBody messageBody;
    newObj.SerializeToString(messageBody.add_message("NewObj"));

    std::string serializedBody;
    messageBody.SerializeToString(&serializedBody);
    sendViaSpace(messageHeader, MemoryReference(serializedBody));
}

void HostedObject::initializeDefault(
    const String&mesh,
    const LightInfo *lightInfo,
    const String&webViewURL,
    const Vector3f&meshScale,
    const PhysicalParameters&physicalParameters)
{
    mObjectHost->registerHostedObject(getSharedPtr());
    Duration ttl = defaultTTL->as<Duration>();
    if (!mesh.empty()) {
        Protocol::StringProperty meshprop;
        meshprop.set_value(mesh);
        meshprop.SerializeToString(propertyPtr("MeshURI", ttl));
        Protocol::Vector3fProperty scaleprop;
        scaleprop.set_value(Vector3f(1,1,1)); // default value, set it manually if you want different.
        scaleprop.SerializeToString(propertyPtr("MeshScale", ttl));
        Protocol::PhysicalParameters physicalprop;
        physicalprop.set_mode(Protocol::PhysicalParameters::NONPHYSICAL);
        physicalprop.SerializeToString(propertyPtr("PhysicalParameters", ttl));
        if (!webViewURL.empty()) {
            Protocol::StringProperty meshprop;
            meshprop.set_value(webViewURL);
            meshprop.SerializeToString(propertyPtr("WebViewURL", ttl));
        }
    } else if (lightInfo) {
        Protocol::LightInfoProperty lightProp;
        lightInfo->toProtocol(lightProp);
        lightProp.SerializeToString(propertyPtr("LightInfo", ttl));
    } else {
        setProperty("IsCamera", ttl);
    }
    //connectToSpace(spaceID, spaceConnectionHint, startingLocation, meshBounds, getUUID());
}

void HostedObject::initializeRestoreFromDatabase(const SpaceID& spaceID, const HostedObjectPtr&spaceConnectionHint) {
    mObjectHost->registerHostedObject(getSharedPtr());

    Persistence::SentReadWriteSet *msg;
    if (mDefaultTracker == NULL) {
        // FIXME this allocation is happening before a real connection to the
        // space.  Currently this ends up just using NULL form
        // default_tracker_port, which obviously won't work out in the long term.
        ODP::Port* default_tracker_port = NULL;
        try {
            default_tracker_port = bindODPPort(spaceID);
        } catch(ODP::PortAllocationException& e) {
        }
        mDefaultTracker = new QueryTracker(default_tracker_port, mObjectHost->getSpaceIO());
        mDefaultTracker->forwardMessagesTo(&mSendService);
    }
    msg = new Persistence::SentReadWriteSet(mDefaultTracker);
    msg->setPersistenceCallback(std::tr1::bind(
                         &PrivateCallbacks::initializeDatabaseCallback,
                         getWeakPtr(), spaceID, spaceConnectionHint,
                         _1, _2, _3));
    msg->body().add_reads().set_field_name("WebViewURL");
    msg->body().add_reads().set_field_name("MeshURI");
    msg->body().add_reads().set_field_name("MeshScale");
    msg->body().add_reads().set_field_name("Name");
    msg->body().add_reads().set_field_name("PhysicalParameters");
    msg->body().add_reads().set_field_name("LightInfo");
    msg->body().add_reads().set_field_name("IsCamera");
    msg->body().add_reads().set_field_name("Parent");
    msg->body().add_reads().set_field_name("Loc");
    msg->body().add_reads().set_field_name("_Script");
    msg->body().add_reads().set_field_name("_ScriptParams");
    for (int i = 0; i < msg->body().reads_size(); i++) {
        msg->body().reads(i).set_object_uuid(getUUID()); // database assumes uuid 0 if omitted
    }
    msg->header().set_destination_object(ObjectReference::spaceServiceID());
    msg->header().set_destination_port(Services::PERSISTENCE);
    msg->serializeSend();
}
namespace {
bool myisalphanum(char c) {
    if (c>='a'&&c<='z') return true;
    if (c>='A'&&c<='Z') return true;
    if (c>='0'&&c<='9') return true;
    return false;
}
}
void HostedObject::initializeScript(const String& script, const ObjectScriptManager::Arguments &args) {
    assert(!mObjectScript); // Don't want to kill a live script!
    static ThreadIdCheck scriptId=ThreadId::registerThreadGroup(NULL);
    assertThreadGroup(scriptId);
    mObjectHost->registerHostedObject(getSharedPtr());
    if (!ObjectScriptManagerFactory::getSingleton().hasConstructor(script)) {
        bool passed=true;
        for (std::string::const_iterator i=script.begin(),ie=script.end();i!=ie;++i) {
            if (!myisalphanum(*i)) {
                if (*i!='-'&&*i!='_') {
                    passed=false;
                }
            }
        }
        if (passed) {
            mObjectHost->getScriptPluginManager()->load(DynamicLibrary::filename(script));
        }
    }
    ObjectScriptManager *mgr = ObjectScriptManagerFactory::getSingleton().getConstructor(script)("");
    if (mgr) {
        mObjectScript = mgr->createObjectScript(this->getSharedPtr(), args);
    }
}
void HostedObject::connectToSpace(
        const SpaceID&spaceID,
        const HostedObjectPtr&spaceConnectionHint,
        const Location&startingLocation,
        const BoundingSphere3f &meshBounds,
        const UUID&object_uuid_evidence)
{
    if (spaceID!=SpaceID::null()) {
        //bind script to object...script might be a remote ID, so need to bind download target, etc
        std::tr1::shared_ptr<TopLevelSpaceConnection> topLevelConnection;
        SpaceDataMap::iterator where;
        if (spaceConnectionHint&&(where=spaceConnectionHint->mSpaceData->find(spaceID))!=spaceConnectionHint->mSpaceData->end()) {
            topLevelConnection=where->second.mSpaceConnection.getTopLevelStream();
        }else {
            topLevelConnection=mObjectHost->connectToSpace(spaceID);
        }

        // sending initial packet is done by the script!
        //conn->send(initializationPacket,Network::ReliableOrdered);
        PerSpaceData &psd = cloneTopLevelStream(spaceID,topLevelConnection);
        // return &(psd.mSpaceConnection);
        sendNewObj(startingLocation, meshBounds, spaceID, object_uuid_evidence);
    }
}

void HostedObject::initializePerSpaceData(PerSpaceData& psd, ProxyObjectPtr selfproxy) {
    psd.initializeAs(selfproxy);
    psd.rpcPort->receive( std::tr1::bind(&HostedObject::handleRPCMessage, this, _1, _2) );
    psd.persistencePort->receive( std::tr1::bind(&HostedObject::handlePersistenceMessage, this, _1, _2) );
}

void HostedObject::disconnectFromSpace(const SpaceID &spaceID) {
    SpaceDataMap::iterator where;
    where=mSpaceData->find(spaceID);
    if (where!=mSpaceData->end()) {
        where->second.destroy(getTracker(spaceID));
        mSpaceData->erase(where);
    } else {
        SILOG(cppoh,error,"Attempting to disconnect from space "<<spaceID<<" when not connected to it...");
    }
}


void HostedObject::processRoutableMessage(const RoutableMessageHeader &header, MemoryReference bodyData) {
    {
        SILOG(cppoh,debug,
              '['<<(mInternalObjectReference.toString())<<']'
              << "** Message from: " << header.source_object()
              << " port " << header.source_port()
              << " to "<<(header.has_destination_object()
                          ?  header.destination_object().toString()
                          :  ("[Temporary UUID " + mInternalObjectReference.toString() +"]"))
              << " port " << header.destination_port());
    }
    /// Handle Return values to queries we sent to someone:
    if (header.has_reply_id()) {
        SpaceID space = header.destination_space();
        QueryTracker* responsibleTracker = getTracker(space);
        if (responsibleTracker != NULL)
            responsibleTracker->processMessage(header, bodyData);
        if (mDefaultTracker != NULL)
            mDefaultTracker->processMessage(header, bodyData);
        return; // Not a message for us to process.
    }

    /** NOTE: ODP::Service is the way we should be handling these.  In order to
     *  transition to ODP only at this layer gracefully, we need to leave the
     *  other delivery mechanisms in place.  However, ODP delivery should
     *  *always* be attempted first. RPC already has another path and has been
     *  marked as deprecated.  As other paths are replaced, they should also be
     *  marked.
     */
    if (mDelegateODPService->deliver(header, bodyData)) {
        // if this was true, it got delivered
    } else if (header.destination_port() == 0) {
        DEPRECATED(HostedObject);
        handleRPCMessage(header, bodyData);
    } else {
        if (mObjectScript)
            mObjectScript->processMessage(header, bodyData);
    }
}

void HostedObject::sendViaSpace(const RoutableMessageHeader &hdrOrig, MemoryReference body) {
    //DEPRECATED(HostedObject);
    ///// MessageService::processMessage
    assert(hdrOrig.has_destination_object());
    assert(hdrOrig.has_destination_space());
    SpaceDataMap::iterator where=mSpaceData->find(hdrOrig.destination_space());
    if (where!=mSpaceData->end()) {
        RoutableMessageHeader hdr (hdrOrig);
        hdr.clear_destination_space();
        hdr.clear_source_space();
        hdr.clear_source_object();
        String serialized_header;
        hdr.SerializeToString(&serialized_header);
        where->second.mSpaceConnection.getStream()->send(MemoryReference(serialized_header),body, Network::ReliableOrdered);
    }
    assert(where!=mSpaceData->end());
}

void HostedObject::send(const RoutableMessageHeader &hdrOrig, MemoryReference body) {
    //DEPRECATED(HostedObject);
    assert(hdrOrig.has_destination_object());
    if (!hdrOrig.has_destination_space() || hdrOrig.destination_space() == SpaceID::null()) {
        DEPRECATED(HostedObject); // QueryTracker still causes this case
        RoutableMessageHeader hdr (hdrOrig);
        hdr.set_destination_space(SpaceID::null());
        hdr.set_source_object(ObjectReference(mInternalObjectReference));
        mObjectHost->processMessage(hdr, body);
        return;
    }
    sendViaSpace(hdrOrig, body);
}

void HostedObject::tick() {
    for (SpaceDataMap::iterator iter = mSpaceData->begin(); iter != mSpaceData->end(); ++iter) {
        // send update to LOC (2) service in the space, if necessary
        iter->second.updateLocation(this);
    }
}


static int32 query_id = 0;
using Protocol::LocRequest;
void HostedObject::processRPC(const RoutableMessageHeader &msg, const std::string &name, MemoryReference args, String *response) {
    if (name == "CreateObject") {
                Protocol::CreateObject co;
                co.ParseFromArray(args.data(),args.size());
                PhysicalParameters phys;
                LightInfo light;
                LightInfo *pLight=NULL;
                UUID uuid;
                std::string weburl;
                std::string mesh;
                bool camera=false;
                if (!co.has_object_uuid()) {
                    uuid = UUID::random();
                } else {
                    uuid = co.object_uuid();
                }
                if (!co.has_scale()) {
                    co.set_scale(Vector3f(1,1,1));
                }
                if (co.has_weburl()) {
                    weburl = co.weburl();
                }
                if (co.has_mesh()) {
                    mesh = co.mesh();
                }
                if (co.has_physical()) {
                    parsePhysicalParameters(phys, co.physical());
                }
                if (co.has_light_info()) {
                    pLight=&light;
                    light = LightInfo(co.light_info());
                }
                if (co.has_camera() && co.camera()) {
                    camera=true;
                }
                SILOG(cppoh,info,"Creating new object "<<ObjectReference(uuid));
                VWObjectPtr vwobj = HostedObject::construct<HostedObject>(mObjectHost, uuid);
                std::tr1::shared_ptr<HostedObject>obj=std::tr1::static_pointer_cast<HostedObject>(vwobj);
                if (camera) {
                    obj->initializeDefault("",NULL,"",co.scale(),phys);
                } else {
                    obj->initializeDefault(mesh,pLight,weburl,co.scale(),phys);
                }
                for (int i = 0; i < co.space_properties_size(); ++i) {
                    //RoutableMessageHeader connMessage
                    //obj->processRoutableMessage(connMessageHeader, connMessageData);
                    Protocol::ConnectToSpace space = co.space_properties(i);
                    UUID evidence = space.has_object_uuid_evidence()
                         ? space.object_uuid_evidence()
                         : uuid;
                    SpaceID spaceid (space.has_space_id()?space.space_id():msg.destination_space().getObjectUUID());
                    if (!space.has_bounding_sphere()) {
                        space.set_bounding_sphere(PBJ::BoundingSphere3f(Vector3f(0,0,0),1));
                    }
                    if (space.has_requested_object_loc() && space.has_bounding_sphere()) {
                        BoundingSphere3f bs (space.bounding_sphere());
                        Location location(Vector3d::nil(),Quaternion::identity(),Vector3f::nil(),Vector3f(1,0,0),0);
                        const Protocol::ObjLoc &loc = space.requested_object_loc();
                        SILOG(cppoh,debug,"Creating object "<<ObjectReference(getUUID())
                                <<" at position "<<loc.position());
                        if (loc.has_position()) {
                            location.setPosition(loc.position());
                        }
                        if (loc.has_orientation()) {
                            location.setOrientation(loc.orientation());
                        }
                        if (loc.has_velocity()) {
                            location.setVelocity(loc.velocity());
                        }
                        if (loc.has_rotational_axis()) {
                            location.setAxisOfRotation(loc.rotational_axis());
                        }
                        if (loc.has_angular_speed()) {
                            location.setAngularSpeed(loc.angular_speed());
                        }
                        obj->connectToSpace(spaceid, getSharedPtr(), location, bs, evidence);
                    }
                }
                if (co.has_script()) {
                    String script_type = co.script();
                    ObjectScriptManager::Arguments script_args;
                    if (co.has_script_args()) {
                        Protocol::StringMapProperty args_map = co.script_args();
                        assert(args_map.keys_size() == args_map.values_size());
                        for (int i = 0; i < args_map.keys_size(); ++i)
                            script_args[ args_map.keys(i) ] = args_map.values(i);
                    }
                    obj->initializeScript(script_type, script_args);
                }
                return;
    }
    if (name == "InitScript") {
        Protocol::ScriptingInit si;
        si.ParseFromArray(args.data(),args.size());

        if (si.has_script()) {
            String script_type = si.script();
            ObjectScriptManager::Arguments script_args;
            if (si.has_script_args()) {
                Protocol::StringMapProperty args_map = si.script_args();
                assert(args_map.keys_size() == args_map.values_size());
                for (int i = 0; i < args_map.keys_size(); ++i)
                    script_args[ args_map.keys(i) ] = args_map.values(i);
            }
            initializeScript(script_type, script_args);
        }

        return;
    }
    if (name == "ConnectToSpace") {
        // Fixme: move connection logic here so it's possible to reply later on.
        return;
    }
    if (name == "DisconnectFromSpace") {
        Protocol::DisconnectFromSpace disCon;
        disCon.ParseFromArray(args.data(),args.size());
        this->disconnectFromSpace(SpaceID(disCon.space_id()));
        return;
    }
    if (name=="DestroyObject") {
        this->destroy();
        return;
    }
    std::ostringstream printstr;
    printstr<<"\t";
    ProxyObjectPtr thisObj = getProxy(msg.source_space());
    VWObject::processRPC(msg,name,args,response);
    if (name == "LocRequest") {
        LocRequest query;
        printstr<<"LocRequest: ";
        query.ParseFromArray(args.data(), args.length());
        Protocol::ObjLoc loc;
        Time now = Time::now(getSpaceTimeOffset(msg.source_space()));
        if (thisObj) {
            Location globalLoc = thisObj->globalLocation(now);
            loc.set_timestamp(now);
            uint32 fields = 0;
            bool all_fields = true;
            if (query.has_requested_fields()) {
                fields = query.requested_fields();
                all_fields = false;
            }
            if (all_fields || (fields & LocRequest::POSITION))
                loc.set_position(globalLoc.getPosition());
            if (all_fields || (fields & LocRequest::ORIENTATION))
                loc.set_orientation(globalLoc.getOrientation());
            if (all_fields || (fields & LocRequest::VELOCITY))
                loc.set_velocity(globalLoc.getVelocity());
            if (all_fields || (fields & LocRequest::ROTATIONAL_AXIS))
                loc.set_rotational_axis(globalLoc.getAxisOfRotation());
            if (all_fields || (fields & LocRequest::ANGULAR_SPEED))
                loc.set_angular_speed(globalLoc.getAngularSpeed());
            if (response)
                loc.SerializeToString(response);
        } else {
            SILOG(objecthost, error, "LocRequest message not for any known object.");
        }
        return;             /// comment out if we want scripts to see these requests
    }
    else if (name == "SetLoc") {
        Protocol::ObjLoc setloc;
        printstr<<"Someone wants to set my position: ";
        setloc.ParseFromArray(args.data(), args.length());
        if (thisObj) {
            printstr<<setloc.position();
            applyPositionUpdate(thisObj, setloc, false);
        }
    }
    else if (name == "AddObject") {
        Protocol::ObjLoc setloc;
        printstr<<"Someone wants to set my position: ";
        setloc.ParseFromArray(args.data(), args.length());
        if (thisObj) {
            printstr<<setloc.position();
            applyPositionUpdate(thisObj, setloc, false);
        }
    }
    else if (name == "DelObj") {
        SpaceDataMap::iterator perSpaceIter = mSpaceData->find(msg.source_space());
        if (perSpaceIter == mSpaceData->end()) {
            SILOG(objecthost, error, "DelObj message not for any known space.");
            return;
        }
        TopLevelSpaceConnection *proxyMgr =
            perSpaceIter->second.mSpaceConnection.getTopLevelStream().get();
        if (thisObj && proxyMgr) {
            proxyMgr->unregisterHostedObject(thisObj->getObjectReference().object());
        }
    }
    else if (name == "RetObj") {
        SpaceID space = msg.source_space();
        SpaceDataMap::iterator perSpaceIter = mSpaceData->find(space);
        if (msg.source_object() != ObjectReference::spaceServiceID()) {
            SILOG(objecthost, error, "RetObj message not coming from space: "<<msg.source_object());
            return;
        }
        if (perSpaceIter == mSpaceData->end()) {
            SILOG(objecthost, error, "RetObj message not for any known space.");
            return;
        }
        // getProxyManager() does not work because we have not yet created our ProxyObject.
        TopLevelSpaceConnection *proxyMgr =
            perSpaceIter->second.mSpaceConnection.getTopLevelStream().get();

        Protocol::RetObj retObj;
        retObj.ParseFromArray(args.data(), args.length());
        if (retObj.has_object_reference() && retObj.has_location()) {
            SpaceObjectReference objectId(msg.source_space(), ObjectReference(retObj.object_reference()));
            perSpaceIter->second.setObject(objectId.object());
            ProxyObjectPtr proxyObj;
            if (hasProperty("IsCamera")) {
                printstr<<"RetObj: I am now a Camera known as "<<objectId.object();
                proxyObj = ProxyObjectPtr(new ProxyCameraObject(proxyMgr, objectId, this));
            } else if (hasProperty("LightInfo") && !hasProperty("MeshURI")) {
                printstr<<"RetObj. I am now a Light known as "<<objectId.object();
                proxyObj = ProxyObjectPtr(new ProxyLightObject(proxyMgr, objectId, this));
            } else if (hasProperty("MeshURI") && hasProperty("WebViewURL")){
                printstr<<"RetObj: I am now a WebView known as "<<objectId.object();
                proxyObj = ProxyObjectPtr(new ProxyWebViewObject(proxyMgr, objectId, this));
            } else {
                printstr<<"RetObj: I am now a Mesh known as "<<objectId.object();
                proxyObj = ProxyObjectPtr(new ProxyMeshObject(proxyMgr, objectId, this));
            }
            proxyObj->setLocal(true);
            initializePerSpaceData(perSpaceIter->second, proxyObj);
            proxyMgr->registerHostedObject(objectId.object(), getSharedPtr());
            applyPositionUpdate(proxyObj, retObj.location(), true);
            perSpaceIter->second.locationWasReset(retObj.location().timestamp(), proxyObj->getLastLocation());
            if (proxyMgr) {
                proxyMgr->createObject(proxyObj, getTracker(space));
                ProxyCameraObject* cam = dynamic_cast<ProxyCameraObject*>(proxyObj.get());
                if (cam) {
                    /* HACK: Because we have no method of scripting yet, we force
                       any local camera we create to attach for convenience. */
                    cam->attach(String(), 0, 0);
                    uint32 my_query_id = query_id;
                    query_id++;
                    Protocol::NewProxQuery proxQuery;
                    proxQuery.set_query_id(my_query_id);
                    proxQuery.set_max_radius(1.0e+30f);
                    String proxQueryStr;
                    proxQuery.SerializeToString(&proxQueryStr);
                    RoutableMessageBody body;
                    body.add_message("NewProxQuery", proxQueryStr);
                    String bodyStr;
                    body.SerializeToString(&bodyStr);
                    RoutableMessageHeader proxHeader;
                    proxHeader.set_destination_port(Services::GEOM);
                    proxHeader.set_destination_object(ObjectReference::spaceServiceID());
                    proxHeader.set_destination_space(objectId.space());
                    send(proxHeader, MemoryReference(bodyStr));
                }
                for (PropertyMap::const_iterator iter = mProperties.begin();
                        iter != mProperties.end();
                        ++iter) {
                    receivedPropertyUpdate(proxyObj, iter->first, iter->second.mData);
                }
            }
        }
    } else {
        printstr<<"Message to be handled in script: "<<name;
    }
    SILOG(cppoh,debug,printstr.str());
    if (mObjectScript) {
        MemoryBuffer returnCopy;
        mObjectScript->processRPC(msg, name, args, returnCopy);
        if (response) {
            response->reserve(returnCopy.size());
            std::copy(returnCopy.begin(), returnCopy.end(),
                      std::insert_iterator<std::string>(*response, response->begin()));
        }
    }
}
const Duration&HostedObject::getSpaceTimeOffset(const SpaceID&space) {
    static Duration nil(Duration::seconds(0));
    SpaceDataMap::iterator where=mSpaceData->find(space);
    if (where!=mSpaceData->end())
        return where->second.mSpaceConnection.getTopLevelStream()->getServerTimeOffset();
    return nil;
}

ProxyManager* HostedObject::getProxyManager(const SpaceID&space) {
    SpaceDataMap::iterator iter = mSpaceData->find(space);
    if (iter == mSpaceData->end()) {
        return NULL;
    }
    return iter->second.mSpaceConnection.getTopLevelStream().get();
}
bool HostedObject::isLocal(const SpaceObjectReference&objref) const{
    SpaceDataMap::const_iterator iter = mSpaceData->find(objref.space());
    HostedObjectPtr destHostedObj;
    if (iter != mSpaceData->end()) {
        destHostedObj = iter->second.mSpaceConnection.getTopLevelStream()->getHostedObject(objref.object());
    }
    if (destHostedObj) {
        // This object is local to our object host--no need to query for its position.
        return true;
    }
    return false;
}


void HostedObject::removeQueryInterest(uint32 query_id, const ProxyObjectPtr&proxyObj, const SpaceObjectReference&proximateObjectId) {
    SpaceID space = proximateObjectId.space();
    SpaceDataMap::iterator where = mSpaceData->find(space);
    if (where !=mSpaceData->end()) {
        ProxyManager* proxyMgr=where->second.mSpaceConnection.getTopLevelStream().get();
        PerSpaceData::ProxQueryMap::iterator iter = where->second.mProxQueryMap.find(query_id);
        if (iter != where->second.mProxQueryMap.end()) {
            std::set<ObjectReference>::iterator proxyiter = iter->second.find(proximateObjectId.object());
            assert (proxyiter != iter->second.end());
            if (proxyiter != iter->second.end()) {
                iter->second.erase(proxyiter);
                //FIXME slow: iterate through all outstanding queries for this object to see if others still refer to this
                bool otherCopies=false;
                for (PerSpaceData::ProxQueryMap::const_iterator i= where->second.mProxQueryMap.begin(),
                         ie=where->second.mProxQueryMap.end();
                     i!=ie;
                     ++i) {
                    if (i->second.find(proximateObjectId.object())!=i->second.end()) {
                        otherCopies=true;
                        break;
                    }
                }
                if (!otherCopies) {
                    proxyMgr->destroyObject(proxyObj, this->getTracker(space));
                }
            }
        }
    }
}

void HostedObject::addQueryInterest(uint32 query_id, const SpaceObjectReference&proximateObjectId) {
    SpaceDataMap::iterator where = mSpaceData->find(proximateObjectId.space());
    if (where !=mSpaceData->end()) {
        where->second.mProxQueryMap[query_id].insert(proximateObjectId.object());
    }
}



// ODP::Service Interface
ODP::Port* HostedObject::bindODPPort(SpaceID space, ODP::PortID port) {
    return mDelegateODPService->bindODPPort(space, port);
}

ODP::Port* HostedObject::bindODPPort(SpaceID space) {
    return mDelegateODPService->bindODPPort(space);
}

void HostedObject::registerDefaultODPHandler(const ODP::MessageHandler& cb) {
    mDelegateODPService->registerDefaultODPHandler(cb);
}

ODP::DelegatePort* HostedObject::createDelegateODPPort(ODP::DelegateService* parentService, SpaceID space, ODP::PortID port) {
    assert(space != SpaceID::any());
    assert(space != SpaceID::null());

    SpaceDataMap::const_iterator space_data_it = mSpaceData->find(space);
    if (space_data_it == mSpaceData->end())
        throw ODP::PortAllocationException("HostedObject::createDelegateODPPort can't allocate port because the HostedObject is not connected to the specified space.");

    ObjectReference objid = space_data_it->second.object;
    ODP::Endpoint port_ep(space, objid, port);
    return new ODP::DelegatePort(
        mDelegateODPService,
        port_ep,
        std::tr1::bind(
            &HostedObject::delegateODPPortSend, this,
            port_ep, _1, _2
        )
    );
}

bool HostedObject::delegateODPPortSend(const ODP::Endpoint& source_ep, const ODP::Endpoint& dest_ep, MemoryReference payload) {
    assert(source_ep.space() == dest_ep.space());

    SpaceDataMap::iterator space_data_it = mSpaceData->find(dest_ep.space());
    if (space_data_it == mSpaceData->end())
        return false;
    SpaceConnection& space_conn = space_data_it->second.mSpaceConnection;

    RoutableMessageHeader hdr;
    hdr.set_source_space( source_ep.space() );
    hdr.set_source_object( source_ep.object() );
    hdr.set_source_port( source_ep.port() );
    hdr.set_destination_space( dest_ep.space() );
    hdr.set_destination_object( dest_ep.object() );
    hdr.set_destination_port( dest_ep.port() );

    String serialized_hdr;
    hdr.SerializeToString(&serialized_hdr);
    MemoryReference hdr_data(serialized_hdr);

    RoutableMessageBody body;
    body.set_payload(payload.data(), payload.size());
    String serialized_body;
    body.SerializeToString(&serialized_body);
    MemoryReference body_data(serialized_body);

    return space_conn.getStream()->send(hdr_data, body_data, Network::ReliableOrdered);
}

}
