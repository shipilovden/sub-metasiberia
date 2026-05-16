/*=====================================================================
NetDownloadResourcesThread.cpp
------------------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "NetDownloadResourcesThread.h"


#include "../shared/Protocol.h"
#include "HTTPClient.h"
#include "URL.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>
#include <unordered_map>


namespace
{
struct PendingNetResource
{
	PendingNetResource() : priority(0) {}
	PendingNetResource(const URLString& url_, int priority_) : url(url_), priority(priority_) {}

	URLString url;
	int priority;

	bool operator<(const PendingNetResource& other) const
	{
		if(priority != other.priority)
			return priority > other.priority; // Higher priority first.

		return url < other.url;
	}
};


static bool processQueuedNetDownloadMessage(const Reference<ThreadMessage>& msg,
	std::set<PendingNetResource>& pending_resources,
	std::unordered_map<URLString, int, URLStringHasher>& pending_priorities)
{
	if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
	{
		DownloadResourceMessage* const download_msg = msg.downcastToPtr<DownloadResourceMessage>();
		if(download_msg->processed.increment() == 0) // If this is the first thread to process this message:
		{
			auto res = pending_priorities.find(download_msg->URL);
			if(res == pending_priorities.end())
			{
				pending_priorities[download_msg->URL] = download_msg->priority;
				pending_resources.insert(PendingNetResource(download_msg->URL, download_msg->priority));
			}
			else if(download_msg->priority > res->second)
			{
				pending_resources.erase(PendingNetResource(download_msg->URL, res->second));
				res->second = download_msg->priority;
				pending_resources.insert(PendingNetResource(download_msg->URL, download_msg->priority));
			}
		}

		return false;
	}
	else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
	{
		return true;
	}

	return false;
}


static bool collectQueuedNetDownloadMessages(ThreadSafeQueue<Reference<ThreadMessage> >& message_queue, size_t max_messages,
	std::set<PendingNetResource>& pending_resources,
	std::unordered_map<URLString, int, URLStringHasher>& pending_priorities)
{
	js::Vector<Reference<ThreadMessage>, 16> queued_messages;
	queued_messages.reserve(max_messages);

	{
		Lock lock(message_queue.getMutex());
		size_t num_collected = 0;
		while(message_queue.unlockedNonEmpty() && num_collected < max_messages)
		{
			queued_messages.push_back(message_queue.unlockedDequeue());
			++num_collected;
		}
	}

	for(size_t i = 0; i < queued_messages.size(); ++i)
		if(processQueuedNetDownloadMessage(queued_messages[i], pending_resources, pending_priorities))
			return true;

	return false;
}
}


NetDownloadResourcesThread::NetDownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_,
	glare::AtomicInt* num_net_resources_downloading_)
:	out_msg_queue(out_msg_queue_),
	resource_manager(resource_manager_),
	num_net_resources_downloading(num_net_resources_downloading_)
{
#if !defined(EMSCRIPTEN)
	client = new HTTPClient();
	client->additional_headers.push_back("User-Agent: Metasiberia client (vr.metasiberia.com)");
#endif
}


NetDownloadResourcesThread::~NetDownloadResourcesThread()
{
}


void NetDownloadResourcesThread::kill()
{
#if !defined(EMSCRIPTEN)
	should_die = 1;
	client->kill();
#endif
}


// Make sure num_net_resources_downloading gets decremented even in the presence of exceptions.
struct NumResourcesDownloadingDecrementor
{
	~NumResourcesDownloadingDecrementor() { (*num_net_resources_downloading)--; }
	glare::AtomicInt* num_net_resources_downloading;
};


static const bool VERBOSE = false;


void NetDownloadResourcesThread::doRun()
{
#if !defined(EMSCRIPTEN)
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("NetDownloadResourcesThread");

	try
	{
		static const size_t EXTRA_QUEUED_MESSAGES_TO_COLLECT = 24;

		std::set<PendingNetResource> pending_resources;
		std::unordered_map<URLString, int, URLStringHasher> pending_priorities;

		while(1)
		{
			if(pending_resources.empty())
			{
				// Wait on the message queue until we have something to download
				ThreadMessageRef msg;
				getMessageQueue().dequeue(msg);

				if(processQueuedNetDownloadMessage(msg, pending_resources, pending_priorities))
					return;

				if(collectQueuedNetDownloadMessages(getMessageQueue(), EXTRA_QUEUED_MESSAGES_TO_COLLECT, pending_resources, pending_priorities))
					return;
			}
			else
			{
				if(this->should_die != 0)
					return;

				if(collectQueuedNetDownloadMessages(getMessageQueue(), EXTRA_QUEUED_MESSAGES_TO_COLLECT, pending_resources, pending_priorities))
					return;

				NumResourcesDownloadingDecrementor d;
				d.num_net_resources_downloading = this->num_net_resources_downloading;

				const PendingNetResource pending_resource = *pending_resources.begin();
				URLString url = pending_resource.url;
				pending_priorities.erase(url);
				pending_resources.erase(pending_resources.begin());

				ResourceRef resource = resource_manager->getOrCreateResourceForURL(url);

				// Check to see if we have the resource now, we may have downloaded it recently.
				if(resource->getState() != Resource::State_NotPresent)
				{
					//conPrint("Already have file, not downloading.");
				}
				else
				{
					if(VERBOSE) conPrint("NetDownloadResourcesThread: Downloading file '" + toStdString(url) + "'...");

					resource->setState(Resource::State_Transferring);

					try
					{
						std::vector<uint8> data;

						// Parse URL
						const URL url_components = URL::parseURL(toStdString(url));
						if(url_components.scheme == "http" || url_components.scheme == "https")
						{
							if(url_components.host == "gateway.ipfs.io")
								throw glare::Exception("Skipping " + toStdString(url));

							// Download with HTTP client
							client->max_data_size			= 128 * 1024 * 1024; // 128 MB
							client->max_socket_buffer_size	= 128 * 1024 * 1024; // 128 MB
							HTTPClient::ResponseInfo response_info = client->downloadFile(toStdString(url), data);
							if(response_info.response_code != 200)
								throw glare::Exception("HTTP Download failed: (code: " + toString(response_info.response_code) + "): " + response_info.response_message);

							std::string extension;
							if(response_info.mime_type == "image/bmp")
								extension = "bmp";
							else if(response_info.mime_type == "image/jpeg")
								extension = "jpg";
							else if(response_info.mime_type == "image/png")
								extension = "png";
							else if(response_info.mime_type == "image/gif")
								extension = "gif";
							else if(response_info.mime_type == "image/tiff")
								extension = "tif";

							// Add an extension based on the mime type.
							if(!extension.empty())
								if(!hasExtension(resource->getRawLocalPath(), extension))
								{
									resource->setRawLocalPath(resource->getRawLocalPath() + "." + extension);

									// Avoid path being too long for Windows now that we have appended the extension.
									if(resource_manager->getLocalAbsPathForResource(*resource).size() >= 260)
										resource->setRawLocalPath(resource_manager->computeRawLocalPathFromURLHash(resource->URL, extension)); // Computes a path that doesn't contain the filename, just uses a hash of the filename.

									if(VERBOSE) conPrint("Added extension to local path, new local path: " + resource_manager->getLocalAbsPathForResource(*resource));
								}

							

							// Save to disk
							const std::string path = resource_manager->getLocalAbsPathForResource(*resource);
							try
							{
								FileUtils::writeEntireFile(path, (const char*)data.data(), data.size());

								if(VERBOSE) conPrint("NetDownloadResourcesThread: Wrote downloaded file to '" + path + "'. (len=" + toString(data.size()) + ") ");

								resource->setState(Resource::State_Present);
								resource_manager->markAsChanged();

								out_msg_queue->enqueue(new ResourceDownloadedMessage(url, resource));
							}
							catch(FileUtils::FileUtilsExcep& e)
							{
								resource->setState(Resource::State_NotPresent);
								resource_manager->markAsChanged();
								if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while writing file to disk: " + e.what());
							}
						}
						else
							throw glare::Exception("Unknown protocol scheme in URL '" + toStdString(url) + "': '" + url_components.scheme + "'");
					}
					catch(glare::Exception& e)
					{
						resource->setState(Resource::State_NotPresent);
						resource_manager->markAsChanged();
						if(VERBOSE) conPrint("NetDownloadResourcesThread: Error while downloading file: " + e.what());
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("NetDownloadResourcesThread glare::Exception: " + e.what());
	}
#endif
}
