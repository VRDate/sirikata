/*  Sirikata Transfer -- Content Transfer management system
 *  URI.hpp
 *
 *  Copyright (c) 2008, Patrick Reiter Horn
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
/*  Created on: Jan 5, 2009 */

#ifndef SIRIKATA_URI_HPP__
#define SIRIKATA_URI_HPP__

#include "util/Sha256.hpp"
#include "Range.hpp" // defines cache_usize_type, cache_ssize_type

namespace Sirikata {
/// URI.hpp: Fingerprint and URI class
namespace Transfer {

/// simple file ID class--should make no assumptions about which hash.
typedef SHA256 Fingerprint;

class URIContext {
	std::string mProto;
	std::string mHost;
	std::string mUser;
	std::string mDirectory; ///< Does not include initial slash, but includes ending slash.
//	AuthenticationCreds mAuth;

	static inline void resolveParentDirectories(std::string &str) {
		// Do nothing for now.
		/*
		std::string::size_type slashpos = 0;
		while (true) {
			slashpos = str.find('/', slashpos);
			if (slashpos != std::string::npos) {
				std::string dir = str.substr(slashpos, slashpos)

			}
			slashpos++;
		}
		*/
		/*
		while (str[0]=='.'&&str[1]=='.'&&str[2]=='/') {
			str = str.substr(3);
		}
		std::string::size_type nextdotdot = 0;
		while ((nextdotdot = str.find("../", nextdotdot+1)) != std::string::npos) {

		}
		*/
	}

public:
	URIContext() {
	}

	URIContext(const URIContext &parent,
			const std::string &newProto,
			const std::string &newHost)
		: mProto(newProto),
		  mHost(newHost),
		  mUser(parent.username()),
		  mDirectory(parent.basepath()){
	}
	URIContext(const URIContext &parent,
			const std::string &newProto,
			const std::string &newHost,
			const std::string &newUser)
		: mProto(newProto),
		  mHost(newHost),
		  mUser(newUser),
		  mDirectory(parent.basepath()){
	}
	URIContext(const URIContext &parent,
			const std::string newProto,
			const std::string &newHost,
			const std::string &newUser,
			const std::string &newDirectory)
		: mProto(newProto),
		  mHost(newHost),
		  mUser(newUser),
		  mDirectory(newDirectory){
	}
	URIContext(const URIContext &parent,
			const std::string *newProto,
			const std::string *newHost,
			const std::string *newUser,
			const std::string *newDirectory)
		: mProto(newProto?*newProto:parent.proto()),
		  mHost(newHost?*newHost:parent.host()),
		  mUser(newUser?*newUser:parent.username()),
		  mDirectory(newDirectory?*newDirectory:parent.basepath()){
	}
	URIContext(const URIContext &parent, const std::string &identifier)
			: mProto(parent.proto()),
			  mHost(parent.host()),
			  mUser(parent.username()),
			  mDirectory(parent.basepath()) {
		std::string::size_type colonpos = identifier.find(':');
		std::string::size_type startpos = 0;
		if (colonpos != std::string::npos) {
			/* FIXME: Only accept [a-z0-9] as part of the protocol. We don't want an IPv6 address or
			  long filename with a colon in it being misinterpreted as a protocol.
			  Also, port numbers will be preceded by a colon */

			// global path
			mProto = identifier.substr(0, colonpos);
			mUser = std::string();
			startpos = colonpos+1;
		}

		if (identifier.length() > startpos+2 && identifier[startpos]=='/' && identifier[startpos+1]=='/') {
			/* FIXME: IPv6 addresses have colons and are surrounded by braces.
			 * Example: "http://someuser@[2001:5c0:1101:4300::1]:8080/somedir/somefile*/

			// protocol-relative path
			mUser = std::string(); // clear out user (set to blank if unspecified)
			//mHost = std::string(); // we actually keep this the same.

			std::string::size_type atpos, slashpos;
			std::string::size_type beginpos = startpos+2;
			slashpos = identifier.find('/',beginpos);
			atpos = identifier.rfind('@', slashpos);
			// Authenticated URI
			if (atpos != std::string::npos && atpos > beginpos && atpos < slashpos) {
				mUser = identifier.substr(beginpos, atpos-beginpos);
				beginpos = atpos+1;
			}
			// FIXME: should empty hostname inherit form the parent context?
			if (slashpos != beginpos) {
				if (slashpos == std::string::npos) {
					mHost = identifier.substr(beginpos);
				} else {
					mHost = identifier.substr(beginpos, slashpos-beginpos);
				}
			}
			startpos = slashpos;
		}
		if (identifier.length() > startpos && identifier[startpos]=='/') {
			// server-relative path
			std::string::size_type lastdir = identifier.rfind('/');
			if (lastdir > startpos) {
				mDirectory = identifier.substr(startpos+1, lastdir-startpos-1);
			} else {
				mDirectory = std::string();
			}
		} else {
			// directory-relative path -- not implemented here
			std::string::size_type lastdir = identifier.rfind('/');
			if (lastdir != std::string::npos && lastdir > startpos) {
				mDirectory += identifier.substr(startpos, lastdir-startpos);
			}
		}

		resolveParentDirectories(mDirectory);

	}
	URIContext(const std::string newProto,
			const std::string &newHost,
			const std::string &newUser,
			const std::string &newDirectory)
		: mProto(newProto),
		  mHost(newHost),
		  mUser(newUser),
		  mDirectory(newDirectory){
	}

	inline const std::string &proto() const {
		return mProto;
	}
	inline const std::string &username() const {
		return mUser;
	}
	inline const std::string &host() const {
		return mHost;
	}
	inline const std::string &basepath() const {
		return mDirectory;
	}

	inline std::string toString() const {
		return mProto + "://" + (mUser.empty() ? std::string() : (mUser + "@")) +
			mHost + (mDirectory.empty() ? "/" : ("/" + mDirectory + "/"));
	}

	inline bool operator< (const URIContext &other) const {
		/* Note: I am testing user before hostname to keep this ordering
		 * the same as if you used a string version of the URI.
		 */
		if (mProto == other.mProto) {
			if (mUser == other.mUser) {
				if (mHost == other.mHost) {
					return mDirectory < other.mDirectory;
				}
				return mHost < other.mHost;
			}
			return mUser < other.mUser;
		}
		return mProto < other.mProto;
	}

	inline bool operator==(const URIContext &other) const {
		return mDirectory == other.mDirectory &&
			mUser == other.mUser &&
			mHost == other.mHost &&
			mProto == other.mProto;
	}
};

/// Display both the URI string and the corresponding Fingerprint.
inline std::ostream &operator<<(std::ostream &str, const URIContext &urictx) {
	return str << urictx.toString();
}

/// URI stores both a uri string as well as a Fingerprint to verify it.
class URI {
	URIContext mContext;
	std::string mPath; // should have no slashes.
public:
	URI() {
	}

	URI(const URIContext &parentContext, const std::string &url)
			: mContext(parentContext, url) {
		std::string::size_type slash = url.rfind('/');
		if (slash != std::string::npos) {
			// FIXME: handle incomplete URIs correctly
			if (slash > 0 && url[slash-1] == '/' && !(slash > 1 && url[slash-2] == '/')) {
				// this is actually a hostname section... don't copy it into the filename.
				// unless there were three slashes in a row.
				mPath = std::string();
			} else {
				mPath = url.substr(slash+1);
			}
		} else{
			mPath = url;
		}
	}

	inline const URIContext &context() const {
		return mContext;
	}

	/// Returns the protocol used.
	inline const std::string &proto() const {
		return mContext.proto();
	}

	/// Returns the hostname (or empty if there is none).
	inline const std::string &host() const {
		return mContext.host();
	}

	inline const std::string &username() const {
		return mContext.username();
	}

	inline const std::string &basepath() const {
		return mContext.basepath();
	}

	inline const std::string &filename() const {
		return mPath;
	}

	inline std::string fullpath() const {
		if (mContext.basepath().empty()) {
			return '/' + mPath;
		} else {
			return '/' + mContext.basepath() + '/' + mPath;
		}
	}

	/// const accessor for the full string URI
	inline std::string toString () const {
		return mContext.toString() + mPath;
	}

	/// Compare the URI
	inline bool operator<(const URI &other) const {
		// We can ignore the hash if it references the same URL.
		if (mContext == other.mContext) {
			return mPath < other.mPath;
		}
		return mContext < other.mContext;
	}

	/// Check the URI for equality
	inline bool operator==(const URI &other) const {
		// We can ignore the hash if it references the same URL.
		return mPath == other.mPath && mContext == other.mContext;
	}

};

/// Display both the URI string (including its context).
inline std::ostream &operator<<(std::ostream &str, const URI &uri) {
	return str << uri.toString();
}

class RemoteFileId {
	Fingerprint mHash;
	URI mURI;

public:
	RemoteFileId() {
	}

	RemoteFileId(const Fingerprint &fingerprint, const URI &uri)
			: mHash(fingerprint), mURI(uri) {
	}

	explicit RemoteFileId(const URI &fingerprinturi)
			: mURI(fingerprinturi) {
		const std::string &path = fingerprinturi.filename();
		mHash = Fingerprint::convertFromHex(path);
	}

	/// accessor for the hashed value
	inline const Fingerprint &fingerprint() const {
		return mHash;
	}

	/// const accessor for the full string URI
	inline const URI &uri() const {
		return mURI;
	}
	/// accessor for the full URI
	inline URI &uri() {
		return mURI;
	}
};

}
}

#endif /* SIRIKATA_URI_HPP__ */
