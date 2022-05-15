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
#ifndef _PASSENGER_CGROUP_CPP_
#define _PASSENGER_CGROUP_CPP_

#include <Utils/Cgroup.h>

namespace Passenger {
	int initializeControlGroup(const CgroupControllerInfo &info) {
		int ret;
		pid_t pid = getpid();

		if (0 == info.uid && info.cgname.empty()) {
			return 0;
		}

		if ((ret = cgroup_init()) > 0) {
			return ret;
		}

		if (info.cgname.empty()) {
			return cgroup_change_cgroup_uid_gid_flags(
					info.uid, info.gid, pid, 0
			);
		}

		return assignCgroup(info.cgname.c_str());
    }


	int assignCgroup(const char* cgname) {
		struct cgroup *mygroup;
		int ret;

		if (NULL == (mygroup = cgroup_new_cgroup(cgname))) {
			fprintf(stderr, "*** ERROR ***: cannot allocate cgroup %s resources",
					cgname
			);
			return 1;
		} else if (0 < (ret = cgroup_get_cgroup(mygroup))) {
			cgroup_free(&mygroup);
			fprintf(stderr, "*** ERROR ***: cannot get cgroup %s: %s",
					cgname,
					cgroup_strerror(ret)
			);
			return 1;
		} else if (0 < (ret = cgroup_attach_task(mygroup))) {
			cgroup_free(&mygroup);
			fprintf(stderr, "*** ERROR ***: cannot assign to cgroup %s: %s",
					cgname,
					cgroup_strerror(ret)
			);
			return 1;
		}
		cgroup_free(&mygroup);
		return 0;
	}


} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_CGROUP_H_ */
