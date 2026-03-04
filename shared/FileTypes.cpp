/*=====================================================================
FileTypes.cpp
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "FileTypes.h"


#include "../gui_client/ModelLoading.h"
#include "ImageDecoding.h"


bool FileTypes::hasSupportedExtension(const string_view path)
{
	const string_view extension = getExtensionStringView(path);
	const bool webp_allowed_for_upload_and_transfer = StringUtils::equalCaseInsensitive(extension, "webp");

	return
		ModelLoading::hasSupportedModelExtension(path) ||
		ImageDecoding::hasSupportedImageExtension(path) ||
		webp_allowed_for_upload_and_transfer ||
		hasAudioFileExtension(path) ||
		hasSupportedVideoFileExtension(path);		
}
