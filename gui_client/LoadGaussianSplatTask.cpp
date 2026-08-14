/*=====================================================================
LoadGaussianSplatTask.cpp
=====================================================================*/
#include "LoadGaussianSplatTask.h"

#include "../shared/ResourceManager.h"

#include <utils/Exception.h>
#include <utils/MemMappedFile.h>
#include <utils/RuntimeCheck.h>
#include <utils/UniqueRef.h>
#include <tracy/Tracy.hpp>


LoadGaussianSplatTask::LoadGaussianSplatTask()
:	result_msg_queue(nullptr)
{}


LoadGaussianSplatTask::~LoadGaussianSplatTask()
{}


void LoadGaussianSplatTask::run(size_t /*thread_index*/)
{
	ZoneScopedN("LoadGaussianSplatTask");
	ZoneText(splat_url.c_str(), splat_url.size());

	Reference<GaussianSplatLoadedThreadMessage> message = new GaussianSplatLoadedThreadMessage();
	message->splat_url = splat_url;

	try
	{
		runtimeCheck(resource.nonNull() && resource_manager.nonNull() && result_msg_queue != nullptr);
		const std::string local_path = resource_manager->getLocalAbsPathForResource(*resource);
		ArrayRef<uint8> bytes;
#if EMSCRIPTEN
		UniqueRef<MemMappedFile> mapped_file;
		if(resource->external_resource)
		{
			mapped_file.set(new MemMappedFile(local_path));
			bytes = ArrayRef<uint8>((const uint8*)mapped_file->fileData(), mapped_file->fileSize());
		}
		else
		{
			runtimeCheck(loaded_buffer.nonNull());
			bytes = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
		}
#else
		MemMappedFile mapped_file(local_path);
		bytes = ArrayRef<uint8>((const uint8*)mapped_file.fileData(), mapped_file.fileSize());
#endif
		message->data = GaussianSplatDecoder::decode(splat_url, bytes);
	}
	catch(glare::Exception& e)
	{
		message->error_message = e.what();
	}
	catch(std::exception& e)
	{
		message->error_message = e.what();
	}
	catch(...)
	{
		message->error_message = "Unknown error while decoding Gaussian splat data.";
	}

	result_msg_queue->enqueue(message);
}
