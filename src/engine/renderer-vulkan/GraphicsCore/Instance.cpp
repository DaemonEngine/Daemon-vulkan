/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2025 Daemon Developers
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
// Instance.cpp

// #define VK_NO_PROTOTYPES

#include <SDL3/SDL_vulkan.h>

#include "../Math/NumberTypes.h"
#include "../Memory/DynamicArray.h"

#include "../Error.h"

#include "Vulkan.h"
#include "../VulkanLoader/VulkanLoadFunctions.h"

#include "SwapChain.h"

#include "CapabilityPack.h"
#include "EngineConfig.h"
#include "QueuesConfig.h"
#include "PhysicalDevice.h"
#include "Queue.h"

#include "GraphicsCoreStore.h"

#include "Instance.h"

Instance coreInstance;

void Instance::Init( const char* engineName, const char* appName ) {
	VkApplicationInfo appInfo{
		.pApplicationName = appName,
		.applicationVersion = 0,
		.pEngineName = engineName,
		.engineVersion = 0,
		.apiVersion = VK_MAKE_API_VERSION( 0, 1, 3, 0 )
	};

	uint32 count;
	const char* const* sdlext = SDL_Vulkan_GetInstanceExtensions( &count );

	for ( uint32 i = 0; i < count; i++ ) {
		Log::Notice( sdlext[i] );
	}

	VulkanLoaderInit();

	uint32 extCount;
	vkEnumerateInstanceExtensionProperties( nullptr, &extCount, nullptr );

	DynamicArray<VkExtensionProperties> availableExtensions;
	availableExtensions.Resize( extCount );

	vkEnumerateInstanceExtensionProperties( nullptr, &extCount, availableExtensions.memory );

	DynamicArray<const char*> extensions;
	extensions.Resize( count + capabilityPackMinimal.requiredInstanceExtensions.size );
	memcpy( extensions.memory, sdlext, count * sizeof( const char* ) );
	memcpy( extensions.memory + count, capabilityPackMinimal.requiredInstanceExtensions.memory,
		capabilityPackMinimal.requiredInstanceExtensions.size * sizeof( const char* ) );

	VkInstanceCreateInfo createInfo {
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = count, // ( uint32 ) extensions.elements,
		.ppEnabledExtensionNames = sdlext // extensions.memory
	};

	VkResult res = vkCreateInstance( &createInfo, nullptr, &instance );
	Q_UNUSED( res );

	VulkanLoadInstanceFunctions( instance );

	uint32 deviceCount;
	vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );

	DynamicArray<VkPhysicalDevice> availableDevices;
	availableDevices.Resize( deviceCount );

	vkEnumeratePhysicalDevices( instance, &deviceCount, availableDevices.memory );

	VkPhysicalDeviceFeatures2 f {};
	VkPhysicalDeviceProperties2 p {};

	vkGetPhysicalDeviceFeatures2( availableDevices[1], &f );
	vkGetPhysicalDeviceProperties2( availableDevices[1], &p );

	if ( !SelectPhysicalDevice( availableDevices, &engineConfig, &physicalDevice ) ) {
		return;
	}

	QueuesConfig queuesConfig = GetQueuesConfigForDevice( physicalDevice );

	CreateDevice( physicalDevice, engineConfig, queuesConfig,
		capabilityPackMinimal.requiredExtensions.memory, capabilityPackMinimal.requiredExtensions.size, &device );

	VulkanLoadDeviceFunctions( device );

	mainSwapChain.Init( instance );

	std::string foundQueues = "Found queues: graphics (present: true)";
	graphicsQueue.Init(     device, queuesConfig.graphicsQueue.id, queuesConfig.graphicsQueue.queues );

	uint32 presentSupported;
	vkGetPhysicalDeviceSurfaceSupportKHR( physicalDevice, graphicsQueue.id, mainSwapChain.surface, &presentSupported );

	if ( !presentSupported ) {
		Err( "Graphics queue doesn't support present" );
		return;
	}

	if( queuesConfig.computeQueue.queues ) {
		computeQueue.Init(  device,  queuesConfig.computeQueue.id, queuesConfig.computeQueue.queues );
		vkGetPhysicalDeviceSurfaceSupportKHR( physicalDevice, computeQueue.id, mainSwapChain.surface, &presentSupported );
		foundQueues += Str::Format( ", async compute (present: %s)", ( bool ) presentSupported );
	}

	if ( queuesConfig.transferQueue.queues ) {
		transferQueue.Init( device, queuesConfig.transferQueue.id, queuesConfig.transferQueue.queues );
		foundQueues += Str::Format( ", async transfer" );
	}

	if ( queuesConfig.sparseQueue.queues ) {
		sparseQueue.Init(   device,   queuesConfig.sparseQueue.id, queuesConfig.sparseQueue.queues );
		foundQueues += Str::Format( ", async sparse binding" );
	}

	Log::Notice( foundQueues );

	VkCommandPool pool;
	VkCommandBuffer cmd;
	VkCommandPoolCreateInfo info{
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = graphicsQueue.id
	};

	vkCreateCommandPool( device, &info, nullptr, &pool );

	VkCommandBufferAllocateInfo cmdInfo {
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	vkAllocateCommandBuffers( device, &cmdInfo, &cmd );

	DynamicArray<VkImage> images;
	images.Resize( mainSwapChain.imageCount );

	uint32 imageCount = mainSwapChain.imageCount;
	vkGetSwapchainImagesKHR( device, mainSwapChain.swapChain, &imageCount, images.memory );

	uint32 index;
	vkAcquireNextImageKHR( device, mainSwapChain.swapChain, UINT64_MAX, nullptr, nullptr, &index );

	VkCommandBufferBeginInfo cmdBegin {};

	vkBeginCommandBuffer( cmd, &cmdBegin );

	VkClearColorValue colorClear {
		.float32 = { 1.0f, 0.0f, 0.0f, 1.0f }
	};

	VkImageSubresourceRange ff {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	vkCmdClearColorImage( cmd, images[index], VK_IMAGE_LAYOUT_GENERAL, &colorClear, 1, &ff );
	
	vkEndCommandBuffer( cmd );

	VkSubmitInfo si {
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr
	};
	vkQueueSubmit( graphicsQueue.queues[0].queue, 1, &si, nullptr );

	VkPresentInfoKHR presentInfo{};
	presentInfo.waitSemaphoreCount = 0;
	presentInfo.pWaitSemaphores = nullptr;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &mainSwapChain.swapChain;
	presentInfo.pImageIndices = &index;
	presentInfo.pResults = nullptr;

	vkQueuePresentKHR( graphicsQueue.queues[0].queue, &presentInfo );
}
