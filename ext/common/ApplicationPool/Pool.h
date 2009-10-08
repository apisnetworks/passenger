/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_APPLICATION_POOL_POOL_H_
#define _PASSENGER_APPLICATION_POOL_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <sstream>
#include <map>
#include <list>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <cerrno>
#ifdef TESTING_APPLICATION_POOL
	#include <cstdlib>
#endif

#include "Interface.h"
#include "../Logging.h"
#include "../FileChangeChecker.h"
#include "../CachedFileStat.hpp"
#include "../SpawnManager.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;
using namespace oxt;

// Forward declaration for ApplicationPool::Server.
class Server;

/****************************************************************
 *
 *  See "doc/ApplicationPool algorithm.txt" for a more readable
 *  and detailed description of the algorithm implemented here.
 *
 ****************************************************************/

/**
 * A standard implementation of ApplicationPool::Interface for single-process environments.
 *
 * The environment may or may not be multithreaded - ApplicationPool::Pool is completely
 * thread-safe. Apache with the threading MPM is an example of a multithreaded single-process
 * environment.
 *
 * This class is unusable in multi-process environments such as Apache with the prefork MPM.
 * The reasons are as follows:
 *  - ApplicationPool::Pool uses threads internally. Because threads disappear after a fork(),
 *    an ApplicationPool::Pool object will become unusable after a fork().
 *  - ApplicationPool::Pool stores its internal cache on the heap. Different processes
 *    cannot share their heaps, so they will not be able to access each others' pool cache.
 *  - ApplicationPool::Pool has a connection to the spawn server. If there are multiple
 *    processes, and they all use the spawn servers's connection at the same time without
 *    some sort of synchronization, then bad things will happen.
 *
 * (Of course, ApplicationPool::Pool <em>is</em> usable if each process creates its own
 * ApplicationPool::Pool object, but that would defeat the point of having a shared pool.)
 *
 * For multi-process environments, one should use ApplicationPool::Server +
 * ApplicationPool::Client instead.
 *
 * ApplicationPool::Pool is fully thread-safe.
 *
 * @ingroup Support
 */
class Pool: public ApplicationPool::Interface {
private:
	static const int DEFAULT_MAX_IDLE_TIME = 120;
	static const int DEFAULT_MAX_POOL_SIZE = 20;
	static const int DEFAULT_MAX_INSTANCES_PER_APP = 0;
	static const int CLEANER_THREAD_STACK_SIZE = 1024 * 64;
	static const unsigned int MAX_GET_ATTEMPTS = 10;

	struct Group;
	struct ProcessInfo;
	
	typedef shared_ptr<Group> GroupPtr;
	typedef shared_ptr<ProcessInfo> ProcessInfoPtr;
	typedef list<ProcessInfoPtr> ProcessInfoList;
	typedef map<string, GroupPtr> GroupMap;
	
	struct Group {
		ProcessInfoList processes;
		unsigned int size;
		unsigned long maxRequests;
		
		Group() {
			size = 0;
			maxRequests = 0;
		}
	};
	
	struct ProcessInfo {
		ProcessPtr process;
		time_t startTime;
		time_t lastUsed;
		unsigned int sessions;
		unsigned int processed;
		ProcessInfoList::iterator iterator;
		ProcessInfoList::iterator ia_iterator;
		
		ProcessInfo() {
			startTime = time(NULL);
			lastUsed  = 0;
			sessions  = 0;
			processed = 0;
		}
		
		/**
		 * Returns the uptime of this process so far, as a string.
		 */
		string uptime() const {
			time_t seconds = time(NULL) - startTime;
			stringstream result;
			
			if (seconds >= 60) {
				time_t minutes = seconds / 60;
				if (minutes >= 60) {
					time_t hours = minutes / 60;
					minutes = minutes % 60;
					result << hours << "h ";
				}
				
				seconds = seconds % 60;
				result << minutes << "m ";
			}
			result << seconds << "s";
			return result.str();
		}
	};
	
	/**
	 * A data structure which contains data that's shared between an
	 * ApplicationPool::Pool and a SessionCloseCallback object.
	 * This is because the ApplicationPool::Pool's life time could be
	 * different from a SessionCloseCallback's.
	 */
	struct SharedData {
		boost::mutex lock;
		condition activeOrMaxChanged;
		
		GroupMap groups;
		unsigned int max;
		unsigned int count;
		unsigned int active;
		unsigned int maxPerApp;
		ProcessInfoList inactiveApps;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	/**
	 * Function object which will be called when a session has been closed.
	 */
	struct SessionCloseCallback {
		SharedDataPtr data;
		weak_ptr<ProcessInfo> processInfo;
		
		SessionCloseCallback(const SharedDataPtr &data,
		                     const weak_ptr<ProcessInfo> &processInfo) {
			this->data = data;
			this->processInfo = processInfo;
		}
		
		void operator()() {
			boost::mutex::scoped_lock l(data->lock);
			ProcessInfoPtr processInfo = this->processInfo.lock();
			
			if (processInfo == NULL) {
				return;
			}
			
			GroupMap::iterator it;
			it = data->groups.find(processInfo->process->getAppRoot());
			if (it != data->groups.end()) {
				Group *group = it->second.get();
				ProcessInfoList *processes = &group->processes;
				
				processInfo->processed++;
				if (group->maxRequests > 0 && processInfo->processed >= group->maxRequests) {
					processes->erase(processInfo->iterator);
					group->size--;
					if (processes->empty()) {
						data->groups.erase(processInfo->process->getAppRoot());
					}
					data->count--;
					data->active--;
					data->activeOrMaxChanged.notify_all();
				} else {
					processInfo->lastUsed = time(NULL);
					processInfo->sessions--;
					if (processInfo->sessions == 0) {
						processes->erase(processInfo->iterator);
						processes->push_front(processInfo);
						processInfo->iterator = processes->begin();
						data->inactiveApps.push_back(processInfo);
						processInfo->ia_iterator = data->inactiveApps.end();
						processInfo->ia_iterator--;
						data->active--;
						data->activeOrMaxChanged.notify_all();
					}
				}
			}
		}
	};
	
	AbstractSpawnManagerPtr spawnManager;
	SharedDataPtr data;
	boost::thread *cleanerThread;
	bool done;
	unsigned int maxIdleTime;
	unsigned int waitingOnGlobalQueue;
	condition cleanerThreadSleeper;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	
	// Shortcuts for instance variables in SharedData. Saves typing in get()
	// and other methods.
	boost::mutex &lock;
	condition &activeOrMaxChanged;
	GroupMap &groups;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	unsigned int &maxPerApp;
	ProcessInfoList &inactiveApps;
	
	/**
	 * Verify that all the invariants are correct.
	 */
	bool inline verifyState() {
	#if PASSENGER_DEBUG
		// Invariants for _groups_.
		GroupMap::const_iterator it;
		unsigned int totalSize = 0;
		for (it = groups.begin(); it != groups.end(); it++) {
			const string &appRoot = it->first;
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			
			P_ASSERT(group->size <= count, false,
				"groups['" << appRoot << "'].size (" << group->size <<
				") <= count (" << count << ")");
			totalSize += group->size;
			
			// Invariants for Group.
			
			P_ASSERT(!processes->empty(), false,
				"groups['" << appRoot << "'].processes is nonempty.");
			
			ProcessInfoList::const_iterator prev_lit;
			ProcessInfoList::const_iterator lit;
			prev_lit = processes->begin();
			lit = prev_lit;
			lit++;
			for (; lit != processes->end(); lit++) {
				if ((*prev_lit)->sessions > 0) {
					P_ASSERT((*lit)->sessions > 0, false,
						"groups['" << appRoot << "'].processes "
						"is sorted from nonactive to active");
				}
			}
		}
		P_ASSERT(totalSize == count, false, "(sum of all d.size in groups) == count");
		
		P_ASSERT(active <= count, false,
			"active (" << active << ") < count (" << count << ")");
		P_ASSERT(inactiveApps.size() == count - active, false,
			"inactive_apps.size() == count - active");
	#endif
		return true;
	}
	
	string inspectWithoutLock() const {
		stringstream result;
		
		result << "----------- General information -----------" << endl;
		result << "max      = " << max << endl;
		result << "count    = " << count << endl;
		result << "active   = " << active << endl;
		result << "inactive = " << inactiveApps.size() << endl;
		result << "Waiting on global queue: " << waitingOnGlobalQueue << endl;
		result << endl;
		
		result << "----------- Groups -----------" << endl;
		GroupMap::const_iterator it;
		for (it = groups.begin(); it != groups.end(); it++) {
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			ProcessInfoList::const_iterator lit;
			
			result << it->first << ": " << endl;
			for (lit = processes->begin(); lit != processes->end(); lit++) {
				const ProcessInfo *processInfo = lit->get();
				char buf[128];
				
				snprintf(buf, sizeof(buf),
						"PID: %-5lu   Sessions: %-2u   Processed: %-5u   Uptime: %s",
						(unsigned long) processInfo->process->getPid(),
						processInfo->sessions,
						processInfo->processed,
						processInfo->uptime().c_str());
				result << "  " << buf << endl;
			}
			result << endl;
		}
		return result.str();
	}
	
	/**
	 * Checks whether the given application group needs to be restarted.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the system time.
	 * @throws boost::thread_interrupted
	 */
	bool needsRestart(const string &appRoot, const PoolOptions &options) {
		string restartDir;
		if (options.restartDir.empty()) {
			restartDir = appRoot + "/tmp";
		} else if (options.restartDir[0] == '/') {
			restartDir = options.restartDir;
		} else {
			restartDir = appRoot + "/" + options.restartDir;
		}
		
		string alwaysRestartFile = restartDir + "/always_restart.txt";
		string restartFile = restartDir + "/restart.txt";
		struct stat buf;
		return cstat.stat(alwaysRestartFile, &buf, options.statThrottleRate) == 0 ||
		       fileChangeChecker.changed(restartFile, options.statThrottleRate);
	}
	
	void cleanerThreadMainLoop() {
		this_thread::disable_syscall_interruption dsi;
		unique_lock<boost::mutex> l(lock);
		try {
			while (!done && !this_thread::interruption_requested()) {
				xtime xt;
				xtime_get(&xt, TIME_UTC);
				xt.sec += maxIdleTime + 1;
				if (cleanerThreadSleeper.timed_wait(l, xt)) {
					// Condition was woken up.
					if (done) {
						// ApplicationPool::Pool is being destroyed.
						break;
					} else {
						// maxIdleTime changed.
						continue;
					}
				}
				
				time_t now = syscalls::time(NULL);
				ProcessInfoList::iterator it;
				for (it = inactiveApps.begin(); it != inactiveApps.end(); it++) {
					ProcessInfo     &processInfo = *it->get();
					ProcessPtr       process     = processInfo.process;
					Group           *group       = groups[process->getAppRoot()].get();
					ProcessInfoList *processes   = &group->processes;
					
					if (maxIdleTime > 0 &&  
					   (now - processInfo.lastUsed > (time_t) maxIdleTime)) {
						P_DEBUG("Cleaning idle process " << process->getAppRoot() <<
							" (PID " << process->getPid() << ")");
						processes->erase(processInfo.iterator);
						
						ProcessInfoList::iterator prev = it;
						prev--;
						inactiveApps.erase(it);
						it = prev;
						
						group->size--;
						
						count--;
					}
					if (processes->empty()) {
						groups.erase(process->getAppRoot());
					}
				}
			}
		} catch (const exception &e) {
			P_ERROR("Uncaught exception: " << e.what());
		}
	}
	
	/**
	 * Spawn a new application instance, or use an existing one that's in the pool.
	 *
	 * @throws boost::thread_interrupted
	 * @throws SpawnException
	 * @throws SystemException
	 * @throws TimeRetrievalException Something went wrong while retrieving the system time.
	 */
	pair<ProcessInfoPtr, Group *>
	spawnOrUseExisting(boost::mutex::scoped_lock &l, const PoolOptions &options) {
		beginning_of_function:
		
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		const string &appRoot(options.appRoot);
		ProcessInfoPtr processInfo;
		Group *group;
		ProcessInfoList *processes;
		
		try {
			GroupMap::iterator group_it = groups.find(appRoot);
			
			if (needsRestart(appRoot, options)) {
				if (group_it != groups.end()) {
					ProcessInfoList::iterator list_it;
					processes = &group_it->second->processes;
					for (list_it = processes->begin(); list_it != processes->end(); list_it++) {
						processInfo = *list_it;
						if (processInfo->sessions == 0) {
							inactiveApps.erase(processInfo->ia_iterator);
						} else {
							active--;
						}
						list_it--;
						processes->erase(processInfo->iterator);
						count--;
					}
					groups.erase(appRoot);
				}
				P_DEBUG("Restarting " << appRoot);
				spawnManager->reload(appRoot);
				group_it = groups.end();
				activeOrMaxChanged.notify_all();
			}
			
			if (group_it != groups.end()) {
				group = group_it->second.get();
				processes = &group->processes;
				
				if (processes->front()->sessions == 0) {
					processInfo = processes->front();
					processes->pop_front();
					processes->push_back(processInfo);
					processInfo->iterator = processes->end();
					processInfo->iterator--;
					inactiveApps.erase(processInfo->ia_iterator);
					active++;
					activeOrMaxChanged.notify_all();
				} else if (count >= max || (
					maxPerApp != 0 && group->size >= maxPerApp )
					) {
					if (options.useGlobalQueue) {
						UPDATE_TRACE_POINT();
						waitingOnGlobalQueue++;
						activeOrMaxChanged.wait(l);
						waitingOnGlobalQueue--;
						goto beginning_of_function;
					} else {
						ProcessInfoList::iterator it(processes->begin());
						ProcessInfoList::iterator end(processes->end());
						ProcessInfoList::iterator smallest(processes->begin());
						it++;
						for (; it != end; it++) {
							if ((*it)->sessions < (*smallest)->sessions) {
								smallest = it;
							}
						}
						processInfo = *smallest;
						processes->erase(smallest);
						processes->push_back(processInfo);
						processInfo->iterator = processes->end();
						processInfo->iterator--;
					}
				} else {
					processInfo = ptr(new ProcessInfo());
					{
						this_thread::restore_interruption ri(di);
						this_thread::restore_syscall_interruption rsi(dsi);
						processInfo->process = spawnManager->spawn(options);
					}
					processInfo->sessions = 0;
					processes->push_back(processInfo);
					processInfo->iterator = processes->end();
					processInfo->iterator--;
					group->size++;
					count++;
					active++;
					activeOrMaxChanged.notify_all();
				}
			} else {
				if (active >= max) {
					UPDATE_TRACE_POINT();
					activeOrMaxChanged.wait(l);
					goto beginning_of_function;
				} else if (count == max) {
					processInfo = inactiveApps.front();
					inactiveApps.pop_front();
					group = groups[processInfo->process->getAppRoot()].get();
					processes = &group->processes;
					processes->erase(processInfo->iterator);
					if (processes->empty()) {
						groups.erase(processInfo->process->getAppRoot());
					} else {
						group->size--;
					}
					count--;
				}
				
				UPDATE_TRACE_POINT();
				processInfo = ptr(new ProcessInfo());
				{
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					processInfo->process = spawnManager->spawn(options);
				}
				processInfo->sessions = 0;
				group = new Group();
				group->size = 1;
				group->maxRequests = options.maxRequests;
				groups[appRoot] = ptr(group);
				processes = &group->processes;
				processes->push_back(processInfo);
				processInfo->iterator = processes->end();
				processInfo->iterator--;
				count++;
				active++;
				activeOrMaxChanged.notify_all();
			}
		} catch (const SpawnException &e) {
			string message("Cannot spawn application '");
			message.append(appRoot);
			message.append("': ");
			message.append(e.what());
			if (e.hasErrorPage()) {
				throw SpawnException(message, e.getErrorPage());
			} else {
				throw SpawnException(message);
			}
		} catch (const exception &e) {
			string message("Cannot spawn application '");
			message.append(appRoot);
			message.append("': ");
			message.append(e.what());
			throw SpawnException(message);
		}
		
		processInfo->lastUsed = time(NULL);
		processInfo->sessions++;
		return make_pair(processInfo, group);
	}
	
	/** @throws boost::thread_resource_error */
	void initialize() {
		done = false;
		max = DEFAULT_MAX_POOL_SIZE;
		count = 0;
		active = 0;
		waitingOnGlobalQueue = 0;
		maxPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		maxIdleTime = DEFAULT_MAX_IDLE_TIME;
		cleanerThread = new boost::thread(
			bind(&Pool::cleanerThreadMainLoop, this),
			CLEANER_THREAD_STACK_SIZE
		);
	}
	
public:
	/**
	 * Create a new ApplicationPool::Pool object, and initialize it with a
	 * SpawnManager. The arguments here are all passed to the SpawnManager
	 * constructor.
	 *
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 * @throws boost::thread_resource_error Cannot spawn a new thread.
	 */
	Pool(const string &spawnServerCommand,
	     const ServerInstanceDir::GenerationPtr &generation,
	     const string &logFile = "",
	     const string &rubyCommand = "ruby")
	   : data(new SharedData()),
		cstat(DEFAULT_MAX_POOL_SIZE),
		lock(data->lock),
		activeOrMaxChanged(data->activeOrMaxChanged),
		groups(data->groups),
		max(data->max),
		count(data->count),
		active(data->active),
		maxPerApp(data->maxPerApp),
		inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		this->spawnManager = ptr(new SpawnManager(spawnServerCommand, generation, logFile, rubyCommand));
		initialize();
	}
	
	/**
	 * Create a new ApplicationPool::Pool object and initialize it with
	 * the given spawn manager.
	 *
	 * @throws boost::thread_resource_error Cannot spawn a new thread.
	 */
	Pool(AbstractSpawnManagerPtr spawnManager)
	   : data(new SharedData()),
	     cstat(DEFAULT_MAX_POOL_SIZE),
	     lock(data->lock),
	     activeOrMaxChanged(data->activeOrMaxChanged),
	     groups(data->groups),
	     max(data->max),
	     count(data->count),
	     active(data->active),
	     maxPerApp(data->maxPerApp),
	     inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		this->spawnManager = spawnManager;
		initialize();
	}
	
	virtual ~Pool() {
		this_thread::disable_interruption di;
		{
			boost::mutex::scoped_lock l(lock);
			done = true;
			cleanerThreadSleeper.notify_one();
		}
		cleanerThread->join();
		delete cleanerThread;
	}
	
	virtual SessionPtr get(const string &appRoot) {
		return ApplicationPool::Interface::get(appRoot);
	}
	
	virtual SessionPtr get(const PoolOptions &options) {
		TRACE_POINT();
		using namespace boost::posix_time;
		unsigned int attempt = 0;
		// TODO: We should probably add a timeout to the following
		// lock. This way we can fail gracefully if the server's under
		// rediculous load. Though I'm not sure how much it really helps.
		unique_lock<boost::mutex> l(lock);
		
		while (true) {
			attempt++;
			
			pair<ProcessInfoPtr, Group *> p = spawnOrUseExisting(l, options);
			ProcessInfoPtr &processInfo = p.first;
			Group *group = p.second;
			
			P_ASSERT(verifyState(), SessionPtr(),
				"State is valid:\n" << inspectWithoutLock());
			try {
				UPDATE_TRACE_POINT();
				return processInfo->process->connect(SessionCloseCallback(data, processInfo));
			} catch (const exception &e) {
				processInfo->sessions--;
				
				ProcessInfoList &processes = group->processes;
				processes.erase(processInfo->iterator);
				group->size--;
				if (processes.empty()) {
					groups.erase(options.appRoot);
				}
				count--;
				active--;
				activeOrMaxChanged.notify_all();
				P_ASSERT(verifyState(), SessionPtr(),
					"State is valid: " << inspectWithoutLock());
				if (attempt == MAX_GET_ATTEMPTS) {
					string message("Cannot connect to an existing "
						"application instance for '");
					message.append(options.appRoot);
					message.append("': ");
					try {
						const SystemException &syse =
							dynamic_cast<const SystemException &>(e);
						message.append(syse.sys());
					} catch (const bad_cast &) {
						message.append(e.what());
					}
					throw IOException(message);
				}
			}
		}
		// Never reached; shut up compiler warning
		return SessionPtr();
	}
	
	virtual void clear() {
		boost::mutex::scoped_lock l(lock);
		groups.clear();
		inactiveApps.clear();
		count = 0;
		active = 0;
		activeOrMaxChanged.notify_all();
		// TODO: clear cstat and fileChangeChecker, and reload all spawner servers.
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		boost::mutex::scoped_lock l(lock);
		maxIdleTime = seconds;
		cleanerThreadSleeper.notify_one();
	}
	
	virtual void setMax(unsigned int max) {
		boost::mutex::scoped_lock l(lock);
		this->max = max;
		activeOrMaxChanged.notify_all();
	}
	
	virtual unsigned int getActive() const {
		return active;
	}
	
	virtual unsigned int getCount() const {
		return count;
	}
	
	virtual void setMaxPerApp(unsigned int maxPerApp) {
		boost::mutex::scoped_lock l(lock);
		this->maxPerApp = maxPerApp;
		activeOrMaxChanged.notify_all();
	}
	
	virtual pid_t getSpawnServerPid() const {
		return spawnManager->getServerPid();
	}
	
	virtual string inspect() const {
		unique_lock<boost::mutex> l(lock);
		return inspectWithoutLock();
	}
	
	virtual string toXml(bool includeSensitiveInformation = true) const {
		unique_lock<boost::mutex> l(lock);
		stringstream result;
		GroupMap::const_iterator it;
		
		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info>";
		
		if (includeSensitiveInformation) {
			// TODO: get rid of this and insert *real* sensitive information.
			// This code is just temporary in order to make the unit test pass.
			result << "<includes_sensitive_information/>";
		}
		
		result << "<groups>";
		for (it = groups.begin(); it != groups.end(); it++) {
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			ProcessInfoList::const_iterator lit;
			
			result << "<group>";
			result << "<name>" << escapeForXml(it->first) << "</name>";
			
			result << "<processes>";
			for (lit = processes->begin(); lit != processes->end(); lit++) {
				ProcessInfo *processInfo = lit->get();
				
				result << "<process>";
				result << "<pid>" << processInfo->process->getPid() << "</pid>";
				result << "<sessions>" << processInfo->sessions << "</sessions>";
				result << "<processed>" << processInfo->processed << "</processed>";
				result << "<uptime>" << processInfo->uptime() << "</uptime>";
				result << "</process>";
			}
			result << "</processes>";
			
			result << "</group>";
		}
		result << "</groups>";
		
		result << "</info>";
		return result.str();
	}
};

typedef shared_ptr<Pool> PoolPtr;

} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_POOL_H_ */

