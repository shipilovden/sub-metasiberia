/*=====================================================================
LoadGaussianSplatTask.h
=====================================================================*/
#pragma once

#include "ThreadMessages.h"
#include "../shared/GaussianSplatData.h"
#include "../shared/Resource.h"
#include "../shared/URLString.h"

#include <utils/Task.h>
#include <utils/ThreadMessage.h>
#include <utils/ThreadSafeQueue.h>


class ResourceManager;


class GaussianSplatLoadedThreadMessage : public ThreadMessage
{
public:
	GaussianSplatLoadedThreadMessage() : ThreadMessage(Msg_GaussianSplatLoadedThreadMessage) {}
	GLARE_DISABLE_COPY(GaussianSplatLoadedThreadMessage);

	URLString splat_url;
	GaussianSplatDataRef data;
	std::string error_message;
};


class LoadGaussianSplatTask : public glare::Task
{
public:
	LoadGaussianSplatTask();
	virtual ~LoadGaussianSplatTask();

	virtual void run(size_t thread_index);

	URLString splat_url;
	ResourceRef resource;
	Reference<LoadedBuffer> loaded_buffer;
	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};
