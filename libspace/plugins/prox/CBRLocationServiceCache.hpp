/*  Sirikata
 *  CBRLocationServiceCache.hpp
 *
 *  Copyright (c) 2009, Ewen Cheslack-Postava
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
 *  * Neither the name of libprox nor the names of its contributors may
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

#ifndef _SIRIKATA_CBR_LOCATION_SERVICE_CACHE_HPP_
#define _SIRIKATA_CBR_LOCATION_SERVICE_CACHE_HPP_

#include <sirikata/pintoloc/ExtendedLocationServiceCache.hpp>
#include <prox/base/LocationServiceCache.hpp>
#include <sirikata/space/LocationService.hpp>
#include <boost/thread.hpp>

namespace Sirikata {

/* Implementation of LocationServiceCache which serves Prox libraries;
 * works by listening for updates from our LocationService.  Note that
 * CBR should only be using the LocationServiceListener methods in normal
 * operation -- all other threads are to be used by libprox classes and
 * will only be accessed in the proximity thread. Therefore, most of the
 * work happens in the proximity thread, with the callbacks just storing
 * information to be picked up in the next iteration.
 */
class CBRLocationServiceCache :
        public ExtendedLocationServiceCache,
        public LocationServiceListener
{
public:
    typedef Prox::LocationUpdateListener<ObjectProxSimulationTraits> LocationUpdateListener;

    /** Constructs a CBRLocationServiceCache which caches entries from locservice.  If
     *  replicas is true, then it caches replica entries from locservice, in addition
     *  to the local entries it always caches.
     */
    CBRLocationServiceCache(Network::IOStrand* strand, LocationService* locservice, bool replicas);
    virtual ~CBRLocationServiceCache();

    /* LocationServiceCache members. */
    virtual void addPlaceholderImposter(
        const ObjectID& id,
        const Vector3f& center_offset,
        const float32 center_bounds_radius,
        const float32 max_size,
        const String& query_data,
        const String& mesh
    );
    virtual Iterator startTracking(const ObjectID& id);
    virtual void stopTracking(const Iterator& id);
    virtual bool startRefcountTracking(const ObjectID& id);
    virtual void stopRefcountTracking(const ObjectID& id);

    // ExtendLocationServiceCache
    virtual bool tracking(const ObjectID& id);

    virtual TimedMotionVector3f location(const Iterator& id);
    virtual Vector3f centerOffset(const Iterator& id);
    virtual float32 centerBoundsRadius(const Iterator& id);
    virtual float32 maxSize(const Iterator& id);
    virtual bool isLocal(const Iterator& id);
    String mesh(const Iterator& id);
    String queryData(const Iterator& id);

    virtual const ObjectReference& iteratorID(const Iterator& id);

    virtual void addUpdateListener(LocationUpdateListener* listener);
    virtual void removeUpdateListener(LocationUpdateListener* listener);

    // ExtendLocationServiceCache
    // We also provide accessors by ID for Proximity generate results.
    TimedMotionVector3f location(const ObjectID& id);
    TimedMotionQuaternion orientation(const ObjectID& id);
    AggregateBoundingInfo bounds(const ObjectID& id);
    Transfer::URI mesh(const ObjectID& id);
    String physics(const ObjectID& id);
    String queryData(const ObjectID& id);
    virtual bool aggregate(const ObjectID& id) { return isAggregate(id); }

    const bool isAggregate(const ObjectID& id);


    /* LocationServiceListener members. */
    virtual void localObjectAdded(const UUID& uuid, bool agg, const TimedMotionVector3f& loc, const TimedMotionQuaternion& orient, const AggregateBoundingInfo& bounds, const String& mesh, const String& physics, const String& query_data);
    virtual void localObjectRemoved(const UUID& uuid, bool agg);
    virtual void localLocationUpdated(const UUID& uuid, bool agg, const TimedMotionVector3f& newval);
    virtual void localOrientationUpdated(const UUID& uuid, bool agg, const TimedMotionQuaternion& newval);
    virtual void localBoundsUpdated(const UUID& uuid, bool agg, const AggregateBoundingInfo& newval);
    virtual void localMeshUpdated(const UUID& uuid, bool agg, const String& newval);
    virtual void localPhysicsUpdated(const UUID& uuid, bool agg, const String& newval);
    virtual void localQueryDataUpdated(const UUID& uuid, bool agg, const String& newval);
    virtual void replicaObjectAdded(const UUID& uuid, const TimedMotionVector3f& loc, const TimedMotionQuaternion& orient, const AggregateBoundingInfo& bounds, const String& mesh, const String& physics, const String& query_data);
    virtual void replicaObjectRemoved(const UUID& uuid);
    virtual void replicaLocationUpdated(const UUID& uuid, const TimedMotionVector3f& newval);
    virtual void replicaOrientationUpdated(const UUID& uuid, const TimedMotionQuaternion& newval);
    virtual void replicaBoundsUpdated(const UUID& uuid, const AggregateBoundingInfo& newval);
    virtual void replicaMeshUpdated(const UUID& uuid, const String& newval);
    virtual void replicaPhysicsUpdated(const UUID& uuid, const String& newval);
    virtual void replicaQueryDataUpdated(const UUID& uuid, const String& newval);

private:
    // Object data is only accessed in the prox thread (by libprox
    // and by this class when updates are passed by the main thread).
    // Therefore, this data does *NOT* need to be locked for access.
    struct ObjectData {
        TimedMotionVector3f location;
        TimedMotionQuaternion orientation;
        AggregateBoundingInfo bounds;
        // Whether the object is local or a replica
        bool isLocal;
        String mesh;
        String physics;
        String query_data;
        bool exists; // Exists, i.e. xObjectRemoved hasn't been called
        int16 tracking; // Ref count to support multiple users
        bool isAggregate;
    };


    // These generate and queue up updates from the main thread
    void objectAdded(const UUID& uuid, bool islocal, bool agg, const TimedMotionVector3f& loc, const TimedMotionQuaternion& orient, const AggregateBoundingInfo& bounds, const String& mesh, const String& physics, const String& query_data);
    void objectRemoved(const UUID& uuid, bool agg);
    void locationUpdated(const UUID& uuid, bool agg, const TimedMotionVector3f& newval);
    void orientationUpdated(const UUID& uuid, bool agg, const TimedMotionQuaternion& newval);
    void boundsUpdated(const UUID& uuid, bool agg, const AggregateBoundingInfo& newval);
    void meshUpdated(const UUID& uuid, bool agg, const String& newval);
    void physicsUpdated(const UUID& uuid, bool agg, const String& newval);
    void queryDataUpdated(const UUID& uuid, bool agg, const String& newval);

    // These do the actual work for the LocationServiceListener methods.  Local versions always
    // call these, replica versions only call them if replica tracking is
    // on. Although we now have to lock in these, we put them on the strand
    // instead of processing directly in the methods above so that they don't
    // block any other work.
    void processObjectAdded(const ObjectReference& uuid, ObjectData data);
    void processObjectRemoved(const ObjectReference& uuid, bool agg);
    void processLocationUpdated(const ObjectReference& uuid, bool agg, const TimedMotionVector3f& newval);
    void processOrientationUpdated(const ObjectReference& uuid, bool agg, const TimedMotionQuaternion& newval);
    void processBoundsUpdated(const ObjectReference& uuid, bool agg, const AggregateBoundingInfo& newval);
    void processMeshUpdated(const ObjectReference& uuid, bool agg, const String& newval);
    void processPhysicsUpdated(const ObjectReference& uuid, bool agg, const String& newval);
    void processQueryDataUpdated(const ObjectReference& uuid, bool agg, const String& newval);


    CBRLocationServiceCache();

    typedef boost::recursive_mutex Mutex;
    typedef boost::lock_guard<Mutex> Lock;
    // Separate listener list and rest of data. Some callbacks to listeners can
    // be expensive and we don't want to lock during these (e.g. connections
    // causing insertions with lots of operations and other changes during the
    // insertion).
    Mutex mListenerMutex;
    Mutex mDataMutex;

    Network::IOStrand* mStrand;
    LocationService* mLoc;

    typedef std::set<LocationUpdateListener*> ListenerSet;
    ListenerSet mListeners;

    typedef std::tr1::unordered_map<ObjectReference, ObjectData, ObjectReference::Hasher> ObjectDataMap;
    ObjectDataMap mObjects;
    bool mWithReplicas;

    bool tryRemoveObject(ObjectDataMap::iterator& obj_it);

    // Data contained in our Iterators. We maintain both the UUID and the
    // iterator because the iterator can become invalidated due to ordering of
    // events in the prox thread.
    struct IteratorData {
        IteratorData(const ObjectReference& _objid, ObjectDataMap::iterator _it)
         : objid(_objid), it(_it) {}

        const ObjectReference objid;
        ObjectDataMap::iterator it;
    };

};

} // namespace Sirikata

#endif //_SIRIKATA_CBR_LOCATION_SERVICE_CACHE_HPP_
