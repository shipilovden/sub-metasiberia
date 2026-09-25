/*=====================================================================
ClientSenderThread.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "ClientSenderThread.h"


#include "../shared/Protocol.h"
#include <MySocket.h>
#include <PlatformUtils.h>
#include <ConPrint.h>


ClientSenderThread::ClientSenderThread(Reference<SocketInterface> socket_)
:	socket(socket_)
{}


ClientSenderThread::~ClientSenderThread()
{}


void ClientSenderThread::kill()
{
	{
		Lock lock(mutex); // Serialize with the wait predicate and queue admission.
		should_die = glare::atomic_int(1);
	}

	stuff_to_do_condition.notify();
}


void ClientSenderThread::cancel()
{
	{
		Lock lock(mutex);
		cancelled = glare::atomic_int(1);
		should_die = glare::atomic_int(1);
	}
	stuff_to_do_condition.notify();
}


void ClientSenderThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("ClientSenderThread");

	try
	{
		// This code pattern approximately follows ThreadSafeQueue<T>::dequeue().
		while(1)
		{
			{
				Lock lock(mutex);

				while(data_to_send.empty() && !should_die) // While there is nothing to do yet:
				{
					stuff_to_do_condition.wait(mutex); // Suspend until queue is non-empty, or should_die is set, or we get a spurious wake up.
				}

				if(cancelled || (should_die && data_to_send.empty()))
					break;

				assert(!data_to_send.empty());

				// We don't want to do network writes while holding the mutex, so copy to temp_data_to_send.
				temp_data_to_send = data_to_send;
				data_to_send.clear();
			} // release mutex

			if(temp_data_to_send.nonEmpty())
			{
				socket->writeData(temp_data_to_send.data(), temp_data_to_send.size());
				temp_data_to_send.clear();
			}
		}

		if(cancelled)
			return;

		// Queued updates have been written before the goodbye.
		const uint32 msg_type_and_len[2] = { Protocol::CyberspaceGoodbye, sizeof(uint32) * 2 };
		socket->writeData(msg_type_and_len, sizeof(uint32) * 2);

		socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the server.
	}
	catch(MySocketExcep& e)
	{
		conPrint("ClientSenderThread: Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrint("ClientSenderThread: glare::Exception: " + e.what());
	}
}


void ClientSenderThread::enqueueDataToSend(const ArrayRef<uint8> data)
{
	if(!data.empty())
	{
		{
			// Append data to data_to_send
			Lock lock(mutex);
			if(should_die)
				return;
			const size_t write_i = data_to_send.size();
			data_to_send.resize(write_i + data.size());
			std::memcpy(&data_to_send[write_i], data.data(), data.size());
		}

		stuff_to_do_condition.notify();
	}
}


// Standalone, hermetic regression executable: compile this translation unit with
// CLIENT_SENDER_THREAD_SHUTDOWN_TEST and link the usual glare-core utilities.
// No application startup, real sockets, or server state are involved.
#if defined(CLIENT_SENDER_THREAD_SHUTDOWN_TEST)
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <vector>

namespace
{
void requireShutdownTest(bool ok)
{
	if(!ok)
		std::abort(); // Also bounds a regression that would otherwise hang joining a future.
}

class ShutdownTestSocket : public SocketInterface
{
public:
	explicit ShutdownTestSocket(int block_at_) : block_at(block_at_) {}
	std::mutex mutex;
	std::condition_variable condition;
	const int block_at; // 0: none, 1: data write, 2: graceful close.
	bool entered = false, released = false, interrupted = false, graceful = false;
	std::vector<uint8> bytes;

	void waitAt(int point)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if(point == block_at)
		{
			entered = true;
			condition.notify_all();
			condition.wait(lock, [&] { return released || interrupted; });
		}
		if(interrupted)
			throw MySocketExcep("test cancellation");
	}
	void awaitBlocked()
	{
		std::unique_lock<std::mutex> lock(mutex);
		requireShutdownTest(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
	}
	void release()
	{
		std::lock_guard<std::mutex> lock(mutex);
		released = true;
		condition.notify_all();
	}
	void ungracefulShutdown() override
	{
		std::lock_guard<std::mutex> lock(mutex);
		interrupted = true;
		condition.notify_all();
	}
	void writeData(const void* data, size_t size) override
	{
		waitAt(1);
		const uint8* p = static_cast<const uint8*>(data);
		bytes.insert(bytes.end(), p, p + size);
	}
	void startGracefulShutdown() override { waitAt(2); graceful = true; }
	void waitForGracefulDisconnect() override {}
	size_t readSomeBytes(void*, size_t) override { return 0; }
	void readData(void*, size_t) override {}
	int32 readInt32() override { return 0; }
	uint32 readUInt32() override { return 0; }
	bool endOfStream() override { return true; }
	void writeInt32(int32 x) override { writeData(&x, sizeof(x)); }
	void writeUInt32(uint32 x) override { writeData(&x, sizeof(x)); }
	void setNoDelayEnabled(bool) override {}
	void enableTCPKeepAlive(float) override {}
	void setAddressReuseEnabled(bool) override {}
	void setTimeout(double) override {}
	bool readable(double) override { return false; }
	bool readable(EventFD&) override { return false; }
	IPAddress getOtherEndIPAddress() const override { return IPAddress(); }
	int getOtherEndPort() const override { return 0; }
};

void finishShutdownTest(std::future<void>& run)
{
	requireShutdownTest(run.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	run.get();
}
}

int main()
{
	const uint8 first[] = { 1, 2, 3 }, second[] = { 4, 5 };
	const uint32 goodbye[] = { Protocol::CyberspaceGoodbye, 8 };
	std::vector<uint8> expected(first, first + sizeof(first));
	expected.insert(expected.end(), second, second + sizeof(second));
	expected.insert(expected.end(), (const uint8*)goodbye, (const uint8*)goodbye + sizeof(goodbye));

	// Kill before launch drains accepted updates, and rejects later enqueue attempts.
	{
		Reference<ShutdownTestSocket> socket = new ShutdownTestSocket(0);
		Reference<ClientSenderThread> sender = new ClientSenderThread(socket);
		sender->enqueueDataToSend(ArrayRef<uint8>(first, sizeof(first)));
		sender->enqueueDataToSend(ArrayRef<uint8>(second, sizeof(second)));
		sender->kill();
		sender->enqueueDataToSend(ArrayRef<uint8>(first, sizeof(first)));
		sender->launch();
		auto run = std::async(std::launch::async, [&] { sender->join(); });
		finishShutdownTest(run);
		requireShutdownTest(socket->bytes == expected && socket->graceful);
	}
	// Stop while one batch is in flight: the remaining batch must precede goodbye.
	{
		Reference<ShutdownTestSocket> socket = new ShutdownTestSocket(1);
		Reference<ClientSenderThread> sender = new ClientSenderThread(socket);
		sender->enqueueDataToSend(ArrayRef<uint8>(first, sizeof(first)));
		sender->launch();
		auto run = std::async(std::launch::async, [&] { sender->join(); });
		socket->awaitBlocked();
		sender->enqueueDataToSend(ArrayRef<uint8>(second, sizeof(second)));
		sender->kill();
		socket->release();
		finishShutdownTest(run);
		requireShutdownTest(socket->bytes == expected && socket->graceful);
	}
	// The owner's transport interruption releases either a blocked write or close.
	for(int point = 1; point <= 2; ++point)
	{
		Reference<ShutdownTestSocket> socket = new ShutdownTestSocket(point);
		Reference<ClientSenderThread> sender = new ClientSenderThread(socket);
		sender->enqueueDataToSend(ArrayRef<uint8>(first, sizeof(first)));
		sender->kill();
		sender->launch();
		auto run = std::async(std::launch::async, [&] { sender->join(); });
		socket->awaitBlocked();
		sender->cancel();
		socket->ungracefulShutdown();
		finishShutdownTest(run);
		requireShutdownTest(!socket->graceful);
	}
	// Exercise the wait/kill handoff repeatedly, including an idle sender.
	for(int i = 0; i < 200; ++i)
	{
		Reference<ShutdownTestSocket> socket = new ShutdownTestSocket(0);
		Reference<ClientSenderThread> sender = new ClientSenderThread(socket);
		sender->launch();
		auto run = std::async(std::launch::async, [&] { sender->join(); });
		sender->kill();
		sender->kill();
		finishShutdownTest(run);
		requireShutdownTest(socket->graceful && socket->bytes.size() == sizeof(goodbye));
	}
	conPrint("ClientSenderThread shutdown tests passed");
}
#endif
