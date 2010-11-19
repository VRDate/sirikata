/*  Sirikata
 *  Defs.hpp
 *
 *  Copyright (c) 2010, Ewen Cheslack-Postava
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

#ifndef _SIRIKATA_PROXYOBJECT_DEFS_HPP_
#define _SIRIKATA_PROXYOBJECT_DEFS_HPP_

#include <sirikata/proxyobject/Platform.hpp>

namespace Sirikata {

// Because we use shared pointers for many of these classes and their headers
// frequently need to refer to shared pointers of the other classes, we
// centralize some very important type declarations to avoid needless repetitive
// forward declarations.

class VWObject;
typedef std::tr1::shared_ptr<VWObject> VWObjectPtr;
typedef std::tr1::weak_ptr<VWObject> VWObjectWPtr;

class ProxyObject;
typedef std::tr1::shared_ptr<ProxyObject> ProxyObjectPtr;
typedef std::tr1::weak_ptr<ProxyObject> ProxyObjectWPtr;

class ProxyManager;
typedef std::tr1::shared_ptr<ProxyManager> ProxyManagerPtr;
typedef std::tr1::weak_ptr<ProxyManager> ProxyManagerWPtr;

} // namespace Sirikata

#endif //_SIRIKATA_PROXYOBJECT_DEFS_HPP_