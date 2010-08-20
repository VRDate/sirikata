/*  Meru
 *  ResourceDownloadTask.cpp
 *
 *  Copyright (c) 2009, Stanford University
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

#include "ResourceDownloadPlanner.hpp"
#include <sirikata/proxyobject/ProxyMeshObject.hpp>
#include "../MeshEntity.hpp"
#include <stdlib.h>

using namespace std;
using namespace Sirikata;
using namespace Sirikata::Transfer;
using namespace Sirikata::Graphics;

namespace Sirikata {

ResourceDownloadPlanner::ResourceDownloadPlanner(Provider<ProxyCreationListener*> *proxyManager, Context *c)
 : PollingService(c->mainStrand, Duration::seconds(1), c, "Resource Download Planner Poll")
{
    proxyManager->addListener(this);
}

ResourceDownloadPlanner::~ResourceDownloadPlanner()
{

}

void ResourceDownloadPlanner::onCreateProxy(ProxyObjectPtr p)
{

}

void ResourceDownloadPlanner::onDestroyProxy(ProxyObjectPtr p)
{
    ProxyMeshObjectPtr meshptr(tr1::dynamic_pointer_cast<ProxyMeshObject>(p));
    if (meshptr) {
        meshptr->MeshProvider::removeListener(this);
        MeshEntities.erase(meshptr.get());
    }
}

void ResourceDownloadPlanner::addNewObject(ProxyObjectPtr p, MeshEntity *mesh)
{
    ProxyMeshObjectPtr meshptr(tr1::dynamic_pointer_cast<ProxyMeshObject>(p));
    if (meshptr) {
        meshptr->MeshProvider::addListener(this);
        MeshEntities[meshptr.get()] = mesh;
    }
}

void ResourceDownloadPlanner::onSetMesh(ProxyObjectPtr proxy, URI const &meshFile)
{
   ProxyMeshObjectPtr meshptr(tr1::dynamic_pointer_cast<ProxyMeshObject>(proxy));
   if (meshptr) {
       MeshEntity *mesh = MeshEntities[meshptr.get()];
       if (mesh) mesh->processMesh(meshFile);
   }
}

void ResourceDownloadPlanner::onMeshParsed (ProxyObjectPtr proxy, String const& hash, Meshdata &md)
{

}

void ResourceDownloadPlanner::onSetScale (ProxyObjectPtr proxy, Vector3f const &scale)
{

}

void ResourceDownloadPlanner::onSetPhysical (ProxyObjectPtr proxy, PhysicalParameters const& pp)
{

}

void ResourceDownloadPlanner::poll()
{
    cout<<"POLL"<<endl;
}

void ResourceDownloadPlanner::stop()
{

}
}