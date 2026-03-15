/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2026 Daemon Developers
All rights reserved.

This file is part of the Daemon BSD Source Code (Daemon Source Code).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the Daemon developers nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/
// DebugMsg.cpp

#include "../Math/Bit.h"

#include "Vulkan.h"

#include "GraphicsCoreCVars.h"

#include "GraphicsCoreStore.h"

#include "Instance.h"

#include "DebugMsg.h"

static VkDebugUtilsMessengerEXT debugUtilsMessenger;

static VkBool32 DebugUtilsMsg( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /* pUserData */ ) {
	uint32 severityMask = r_vkDebugMsgSeverity.Get();
	uint32 typeMask     = r_vkDebugMsgType.Get();

	uint32 msgSeverity = ( uint32 ) messageSeverity;

	uint32 severity = 
		  ( ( bool ) msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT ) << DEBUG_MSG_VERBOSE
		| ( ( bool ) msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    ) << DEBUG_MSG_INFO
		| ( ( bool ) msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) << DEBUG_MSG_WARNING
		| ( ( bool ) msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT   ) << DEBUG_MSG_ERROR;

	if ( !( severity & severityMask ) || !( messageTypes & typeMask ) ) {
		return false;
	}

	const char* debugMsgColours[] {
		"^9",
		"^5",
		"^3",
		"^1"
	};

	const char* msgColour = debugMsgColours[severity];

	std::string msg;

	if ( messageTypes & DEBUG_MSG_VALIDATION ) {
		msg = Str::Format( "[VUID: %s]: %s", pCallbackData->pMessageIdName, pCallbackData->pMessage );
	} else {
		msg = Str::Format( "[%s]: %s", pCallbackData->pMessageIdName, pCallbackData->pMessage );
	}

	if ( pCallbackData->objectCount ) {
		const char* name = pCallbackData->pObjects[0].pObjectName ? pCallbackData->pObjects[0].pObjectName : "";

		msg = Str::Format( "[%s %s] %s", string_VkObjectType( pCallbackData->pObjects[0].objectType ), name, msg );
	}

	for ( const VkDebugUtilsObjectNameInfoEXT* obj = pCallbackData->pObjects + 1; obj < pCallbackData->pObjects + pCallbackData->objectCount; obj++ ) {
		const char* name = obj->pObjectName ? obj->pObjectName : "";

		msg = Str::Format( "%s {%s %s}", msg, string_VkObjectType( obj->objectType ), name );
	}

	for ( const VkDebugUtilsLabelEXT* label = pCallbackData->pQueueLabels; label < pCallbackData->pQueueLabels + pCallbackData->queueLabelCount; label++ ) {
		const char* name = label->pLabelName ? label->pLabelName : "";

		msg = Str::Format( "%s {queue %s}", msg, name );
	}

	for ( const VkDebugUtilsLabelEXT* label = pCallbackData->pCmdBufLabels; label < pCallbackData->pCmdBufLabels + pCallbackData->cmdBufLabelCount; label++ ) {
		const char* name = label->pLabelName ? label->pLabelName : "";

		msg = Str::Format( "%s {cmd %s}", msg, name );
	}

	if ( severity >= DEBUG_MSG_WARNING ) {
		Log::Warn(   "%s%s", msgColour, msg );
	} else {
		Log::Notice( "%s%s", msgColour, msg );
	}

	return false;
}

void InitDebugMsg() {
	VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerInfo {
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
		                 | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		                 | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = &DebugUtilsMsg
	};

	vkCreateDebugUtilsMessengerEXT( instance.instance, &debugUtilsMessengerInfo, nullptr, &debugUtilsMessenger );
}