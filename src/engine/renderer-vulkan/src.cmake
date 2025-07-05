set(MATHLIST
    ${ENGINE_DIR}/renderer-vulkan/Math/Bit.h
)

set(MEMORYLIST
    ${ENGINE_DIR}/renderer-vulkan/Memory/Memory.cpp
    ${ENGINE_DIR}/renderer-vulkan/Memory/Memory.h
    ${ENGINE_DIR}/renderer-vulkan/Memory/RingBuffer.cpp
    ${ENGINE_DIR}/renderer-vulkan/Memory/RingBuffer.h
)

set(TASKLIST
    ${ENGINE_DIR}/renderer-vulkan/Thread/Task.h
    ${ENGINE_DIR}/renderer-vulkan/Thread/TaskList.cpp
    ${ENGINE_DIR}/renderer-vulkan/Thread/TaskList.h
    ${ENGINE_DIR}/renderer-vulkan/Thread/Thread.cpp
    ${ENGINE_DIR}/renderer-vulkan/Thread/Thread.h
)

set(UTILSLIST
    ${ENGINE_DIR}/renderer-vulkan/SrcDebug/LogExtend.h
)

set(RENDERERLIST
    ${MATHLIST}
    ${MEMORYLIST}
    ${TASKLIST}
    ${UTILSLIST}
    ${ENGINE_DIR}/renderer-vulkan/MiscCVarStore.cpp
    ${ENGINE_DIR}/renderer-vulkan/RefAPI.cpp
    ${ENGINE_DIR}/renderer-vulkan/RefAPI.h
    ${ENGINE_DIR}/renderer-vulkan/Shared/Timer.cpp
    ${ENGINE_DIR}/renderer-vulkan/Shared/Timer.h
)
