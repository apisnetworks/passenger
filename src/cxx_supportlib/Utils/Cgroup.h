/*
 *  Phusion Passenger cgroup Support
 *  Copyright (c) 2016 Apis Networks
 *
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_CGROUP_H_
#define _PASSENGER_CGROUP_H_

#include <libcgroup.h>
#include <stdlib.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
	struct CgroupControllerInfo {
		uid_t uid;
		gid_t gid;
		string cgname;
	};

    int initializeControlGroup(const CgroupControllerInfo &info);
    int assignCgroup(const char* cgname);

} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_CGROUP_H_ */
