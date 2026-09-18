# vulkanshit — документация

`vulkan*` — это явный (explicit) слой поверх Vulkan API для движка huinya engine.

**Принципы слоя:**
- Без RAII и без vk-bootstrap: каждый `create_*` имеет парный `destroy_*`, которому передаётся та же структура. Деструкторов, убирающих ресурсы автоматически, нет.
- Структуры (`Instance`, `Device`, `Buffer`, ...) — это простые контейнеры сырых Vulkan-хэндлов. Владеет ими вызывающий код.
- Реализация в `src/vulkanshit.cpp`, заголовок — `src/vulkanshit.hpp`.

---

## Оглавление

1. [Базовые типы](#базовые-типы)
2. [Математика](#математика)
3. [Instance](#instance)
4. [Device](#device)
5. [Буферы (Buffer)](#буферы-buffer)
6. [Текстуры (Texture)](#текстуры-texture)
7. [Дескрипторы (Descriptor)](#дескрипторы-descriptor)
8. [Swapchain](#swapchain)
9. [FramePipeline](#framepipeline)
10. [Жизненный цикл](#жизненный-цикл)
11. [Кадр (begin_frame / end_frame)](#кадр-begin_frame--end_frame)
12. [Вспомогательные функции](#вспомогательные-функции)
13. [Шейдеры по умолчанию](#шейдеры-по-умолчанию)
14. [Гайд: как создать треугольник](#гайд-как-создать-треугольник)
15. [Гайд: как нарисовать прямоугольник](#гайд-как-нарисовать-прямоугольник)
16. [Гайд: кастомные прямоугольники](#гайд-кастомные-прямоугольники)

---

## Базовые типы

```cpp
namespace vks {

struct Vec2 { float x, y; };          // 2D-точка/вектор
struct Vec3 { float x, y, z; };       // 3D-вектор
struct Vec4 { float x, y, z, w; };    // 4D-вектор, цвет (RGBA) или позиция с w

// Ось-выровненный прямоугольник на одной плоскости (UV или координаты).
struct Rect { Vec2 min, max; };

struct Color { float r, g, b, a; };   // цвет в диапазоне [0, 1]

// Колоночно-ориентированная матрица 4x4, совместима с GLSL mat4.
struct Mat4 { float m[16]; };

// 4x4 column-major matmul: a * b. GCC auto-vectorizes with -O2/-O3.
Mat4 mat4_mul(const Mat4& a, const Mat4& b);

}
```

Также в заголовке:

```cpp
constexpr uint32_t k_max_frames_in_flight = 2;  // число кадров в полёте
```

## Математика

```cpp
Mat4 ortho_projection(float width, float height);
```

Ортографическая проекция, которая отображает пиксельное пространство с
**началом координат в верхнем левом углу** (ось y растёт вниз) в NDC `[-1, 1]`.
Используйте её для перевода экранных координат в пространство шейдера.

## Instance

```cpp
struct Instance {
    VkInstance               handle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    bool                     validation = false;   // включён ли debug messenger
};

Instance create_instance();
void destroy_instance(Instance& instance);
```

- `create_instance()` создаёт `VkInstance` с расширениями, которые запрашивает
  RGFW (для создания поверхности из окна).
- В **debug-сборке** (`NDEBUG` не задан) запрашиваются validation layers
  (`VK_LAYER_KHRONOS_validation`) + `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` и
  вешается debug messenger. Все сообщения валидации пишутся в `stderr`
  с префиксом `[vk]`. В release-сборке валидация отключена.
- Если validation layers недоступны в debug-сборке — `create_instance()`
  возвращает пустой `Instance`, `handle == VK_NULL_HANDLE`.

## Device

```cpp
struct Device {
    VkInstance       instance = VK_NULL_HANDLE; // для удобства в destroy-путях
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         handle   = VK_NULL_HANDLE;

    uint32_t graphics_family = 0;
    uint32_t present_family  = 0;

    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue  = VK_NULL_HANDLE;
};

Device create_device(const Instance& instance, VkSurfaceKHR surface);
void destroy_device(Device& device);
```

- Выбирает первое подходящее физическое устройство: обязательны графическая и
  present-очереди, поддержка `VK_KHR_SWAPCHAIN` и наличие форматов/present-mode.
- Создаёт логическое устройство с графической и present-очередями (если это
  разные семейства — обе). Очереди уже запрошены в `graphics_queue` / `present_queue`.
- Требует валидную `VkSurfaceKHR` (нужна для проверки present-поддержки).

## Буферы (Buffer)

```cpp
struct Buffer {
    VkBuffer       handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr;   // ненулевой только у host_visible буферов
};

Buffer create_buffer(const Device& device, VkDeviceSize size,
                     VkBufferUsageFlags usage, bool host_visible);
void update_buffer(const Device& device, Buffer& buffer,
                   const void* data, VkDeviceSize size);
void destroy_buffer(const Device& device, Buffer& buffer);
```

- `create_buffer` выделяет память подходящего типа сам:
  - `host_visible = true` → `HOST_VISIBLE | HOST_COHERENT`, буфер сразу
    маппится, `mapped` указывает на персистентный маппинг.
  - `host_visible = false` → `DEVICE_LOCAL`; данные заливаются через
    `update_buffer` (staging-копия).
- `update_buffer`:
  - для host-visible буферов — это обычный `memcpy` в замапленную память;
  - для device-local — создаёт staging-буфер, копирует данные и выполняет
    одноразовый transfer-submit (см. внутренний `submit_single_shot`).
  - если `size` больше вместимости буфера — печатает ошибку и ничего не делает.
- `destroy_buffer` освобождает маппинг, память и буфер; сбрасывает структуру в `{}`.

## Текстуры (Texture)

```cpp
struct Texture {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    uint32_t       width   = 0;
    uint32_t       height  = 0;
};

Texture create_texture(const Device& device, uint32_t width, uint32_t height,
                       const void* pixels);
void destroy_texture(const Device& device, Texture& texture);
```

- Загружает прямоугольные пиксели **RGBA8** (`VK_FORMAT_R8G8B8A8_SRGB`) в
  device-local образ (`VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`),
  делает image view и сэмплер.
- `pixels` — указатель на `width * height * 4` байтов.
- Сэмплер: линейная фильтрация, `CLAMP_TO_EDGE`, mipmap off, координаты
  нормализованные `[0,1]`.
- **Атлас**: одна `Texture` может содержать много спрайтов — нужный кусок
  выбирается UV-прямоугольником при отрисовке.
- `destroy_texture` уничтожает сэмплер, view, память и образ.

## Дескрипторы (Descriptor)

```cpp
struct DescriptorSetLayout { VkDescriptorSetLayout handle = VK_NULL_HANDLE; };
struct DescriptorPool {
    VkDescriptorPool handle = VK_NULL_HANDLE;
    uint32_t         layout_count = 0;  // сколько layout'ов можно использовать в пуле
};

DescriptorSetLayout create_descriptor_layout(const Device& device,
    std::span<const VkDescriptorSetLayoutBinding> bindings);

DescriptorPool create_descriptor_pool(const Device& device,
    std::span<const VkDescriptorPoolSize> pool_sizes,
    uint32_t max_sets,
    uint32_t layout_count = 1);

std::vector<VkDescriptorSet> allocate_descriptor_sets(const Device& device,
    const DescriptorPool& pool, const DescriptorSetLayout& layout, uint32_t count);

void destroy_descriptor_layout(const Device& device, DescriptorSetLayout& layout);
void destroy_descriptor_pool(const Device& device, DescriptorPool& pool);

// Тонкие обёртки над vkUpdateDescriptorSets для типичного 2D-фреймбуфера:

void write_descriptor_buffer(const Device& device, VkDescriptorSet set,
    uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

void write_descriptor_textures(const Device& device, VkDescriptorSet set,
    uint32_t binding, std::span<const VkImageView> views, VkSampler sampler);
```

- `create_descriptor_layout` — layout с произвольным набором биндингов.
- `create_descriptor_pool` — пул; свапчейн может пересоздаваться, поэтому пул
  держит `layout_count` — число layout'ов, из которых разрешено аллоцировать сети.
- `allocate_descriptor_sets` возвращает вектор из `count` сырых
  `VkDescriptorSet` (или пустой вектор при ошибке).
- `write_descriptor_buffer` пишет один `UNIFORM_BUFFER`-дескриптор в биндинг.
- `write_descriptor_textures` пишет массив `COMBINED_IMAGE_SAMPLER` в биндинг
  (все с одним сэмплером и layout'ом `SHADER_READ_ONLY_OPTIMAL`).

## Swapchain

```cpp
struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent = {0, 0};
    std::vector<VkImage> images;

    // Параметры создания, сохраняются для пересборки по VK_ERROR_OUT_OF_DATE_KHR.
    VkSurfaceKHR surface = VK_NULL_HANDLE;   // принадлежит вызывающему коду!
    uint32_t     width   = 0;
    uint32_t     height  = 0;
};

Swapchain create_swapchain(const Device& device, VkSurfaceKHR surface,
                           uint32_t width, uint32_t height);
void destroy_swapchain(const Device& device, Swapchain& swapchain);
```

- `surface` создаётся снаружи (RGFW) и **остаётся во владении вызывающего**:
  `destroy_swapchain` её не уничтожает.
- Формат: предпочитается `B8G8R8A8_SRGB` + `SRGB_NONLINEAR`, present — mailbox,
  при недоступности — FIFO.
- Количество образов: `minImageCount + 1` (обрезается максимумом).
- Если у графической и present-очередей разные семейства — используется
  `VK_SHARING_MODE_CONCURRENT`.

## FramePipeline

`FramePipeline` — полный «граф» одного свапчейна: render pass, пайплайн,
фреймбуферы, командный пул/буферы и синхронизация.

```cpp
struct FramePipeline {
    VkRenderPass     render_pass = VK_NULL_HANDLE;
    VkPipelineLayout layout      = VK_NULL_HANDLE;
    VkPipeline       handle      = VK_NULL_HANDLE;

    std::vector<VkImageView>   image_views;     // по view на свапчейн-образ
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool                command_pool;      // TRANSIENT + RESET_COMMAND_BUFFER
    std::vector<VkCommandBuffer> command_buffers;   // по буферу на свапчейн-образ

    // image_available: пара на кадр в полёте, сигналится vkAcquireNextImageKHR.
    std::vector<VkSemaphore> image_available;
    // render_finished: по одному на свапчейн-образ (продерживается present'ом
    // дольше, чем submit-fence — VUID-vkQueueSubmit-pSignalSemaphores-00067).
    std::vector<VkSemaphore> render_finished;
    std::vector<VkFence>     frame_fences;   // по одному на кадр в полёте
    std::vector<VkFence>     image_fences;   // по одному на свапчейн-образ

    std::string shader_dir;
    size_t      current_frame = 0;
    uint32_t    current_image_index = 0;   // образ, захваченный последним begin_frame

    // Глубокая копия конфига, чтобы recreate_swapchain() мог пересобрать пайплайн.
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    bool alpha_blend   = false;
    bool backface_cull = true;
};

struct PipelineConfig {
    std::span<const VkVertexInputBindingDescription>   bindings;
    std::span<const VkVertexInputAttributeDescription> attributes;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE; // set 0 в пайплайн-layout
    bool alpha_blend   = false;
    bool backface_cull = true;
};

FramePipeline create_frame_pipeline(const Device& device, const Swapchain& swapchain,
                                    const char* shader_dir, const PipelineConfig& config);
void destroy_frame_pipeline(const Device& device, FramePipeline& pipeline);
```

- **Вершинный layout полностью задаёт вызывающий код** через `PipelineConfig` —
  слой не хардкодит вершинный ввод.
- `descriptor_set_layout` запекается в пайплайн-layout как set 0 (если не
  `VK_NULL_HANDLE`).
- Пайплайн: `TRIANGLE_LIST`, front-face **по часовой стрелке** (`CLOCKWISE`),
  cull back-face включён по умолчанию (`backface_cull = true`), viewport/scissor —
  динамические (их выставляет `begin_frame`).
- `alpha_blend = true` включает стандартный премультипликативно-смешательный
  бленд SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
- Шейдеры читаются из `shader_dir` как `vert.spv` и `frag.spv` (важна кратность
  размера 4 байтам — читается прямо в слова).
- `destroy_frame_pipeline` уничтожает всё, включая пул, буферы, синхронизацию.

## Жизненный цикл

Типичный порядок создания (ина obratnogo порядка уничтожения):

1. `vks::create_instance()`
2. создать `VkSurfaceKHR` поверх окна (например через RGFW: `rgfw::createSurface`)
3. `vks::create_device(instance, surface)`
4. `vks::create_swapchain(device, surface, width, height)`
   — создаёт depth image + view внутри `Swapchain` (D32_SFLOAT или
   D24_UNORM_S8_UINT), destroy_swapchain очищает их.
5. `vks::create_descriptor_layout(...)` и `vks::create_descriptor_pool(...)` (по желанию)
6. `vks::create_frame_pipeline(device, swapchain, shader_dir, config)`
7. создать UBO/вершинные буферы, текстуры, `allocate_descriptor_sets` +
   `write_descriptor_*`
8. в цикле: `begin_frame` → запись команд → `end_frame`
9. в конце: `destroy_frame_pipeline`, `destroy_swapchain`, `destroy_device`,
   уничтожить surface (вручную, через `vkDestroySurfaceKHR`), `destroy_instance`

## Кадр (begin_frame / end_frame)

```cpp
VkCommandBuffer begin_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline);
VkResult        end_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline,
                          VkCommandBuffer cmd);

VkResult recreate_swapchain(const Device& device, Swapchain& swapchain, FramePipeline& pipeline);
```

- `begin_frame`:
  - ждёт fence кадра в полёте, захватывает следующий свапчейн-образ
    (`vkAcquireNextImageKHR`);
  - сбрасывает **только свой** командный буфер (`vkResetCommandBuffer`, не весь
    пул — пул общий, а другие образы могут быть ещё «в полёте»);
  - начинает render pass (цвет очищается в чёрный), выставляет viewport и scissor;
  - возвращает командный буфер **уже внутри render pass** — вызывающий код
    записывает в него свои draw-команды.
- При `VK_ERROR_OUT_OF_DATE_KHR` `begin_frame` сам пересоздаёт свапчейн и
  возвращает `VK_NULL_HANDLE` — `end_frame` в этом случае звать НЕ нужно
  (просто пропустите кадр).
- `end_frame` закрывает render pass, делает submit на графической очереди и
  present на present-очереди. При `OUT_OF_DATE`/`SUBOPTIMAL` — пересоздаёт
  свапчейн и возвращает результат. Иначе возвращает `VK_SUCCESS`.
- `recreate_swapchain` пересобирает свапчейн и пайплайн вместе со всеми
  зависимыми объектами. Перед уничтожением делает `vkDeviceWaitIdle` и
  **копирует вершинный layout, дескрипторный layout и опции растеризации в
  локальные переменные** — иначе после `destroy_*` спаны конфига повисли бы.

Минимальный кадр:

```cpp
VkCommandBuffer cmd = vks::begin_frame(device, swapchain, pipeline);
if (cmd == VK_NULL_HANDLE) continue; // OUT_OF_DATE, кадр пересоздан или ошибка

// ... запись: vkCmdBindPipeline, vkCmdBindVertexBuffers,
// vkCmdBindDescriptorSets, vkCmdDraw ...

if (vks::end_frame(device, swapchain, pipeline, cmd) != VK_SUCCESS) break;
```

## Вспомогательные функции

```cpp
void wait_device_idle(Device& device);  // vkDeviceWaitIdle
```

Блокирует до завершения всех работ на устройстве. Полезно перед полным
уничтожением ресурсов.

---

## Шейдеры по умолчанию

В `shaders/vert.glsl` и `shaders/frag.glsl` живёт «квадовый» набор:

- Вершинный ввод (locations): `0 = vec2 position`, `1 = vec2 uv`,
  `2 = vec4 color`, `3 = uint texIndex` (flat).
- UBO `FrameData` — **set 0, binding 0**:
  ```glsl
  layout(set = 0, binding = 0) uniform FrameData {
      mat4 view;
      mat4 proj;
      mat4 view_proj;       // proj * view, baked on CPU
      vec3 camera_pos;      // for specular / IBL
      float _pad0;
      vec3 sun_dir;         // light direction
      float _pad1;
      float time;
      float _pad2;
      float _pad3;
      float _pad4;
  } frame;
  ```
  Поля alignованы для std140: после каждого `vec3` следует `float _pad`.
  `view_proj` умножается на CPU в `mat4_mul` — GCC авто-векторизует при `-O2`.
- Текстуры — **set 0, binding 1**: `uniform sampler2D textures[16];`
  (массив из 16 CDN сэмплеров, индекс выбирается вершинным `texIndex`).
- Фрагментный выход: `outColor = texture(textures[texIndex], uv) * color;`

Это значит: для отрисовки с этими шейдерами дескрипторный сет должен содержать
минимум UBO на биндинге 0 и хотя бы одну текстуру на биндинге 1, а вершинные
атрибуты должны совпадать по layout с указанными locations.

---

## Гайд: как создать треугольник

Ниже — полный путь «с нуля»: один треугольник в центре экрана 800×600,
залитый цветом текстуры (белая текстура 1×1 + цвет через вершинный `color`).

### Шаг 0. Константы и вершинный формат

```cpp
#include <vulkan/vulkan.h>
#include "vulkanshit.hpp"
#include "rgfwshit.hpp"   // RGFW: окно + surface

// Вершина должна совпадать с шейдером: loc0 position, loc1 uv,
// loc2 color, loc3 texIndex.
struct Vertex {
    vks::Vec2 position;
    vks::Vec2 uv;
    vks::Vec4 color;
    uint32_t  tex_index;
};
```

### Шаг 1. Вершинный layout (bindings + attributes)

```cpp
const std::vector<VkVertexInputBindingDescription> bindings = {{
    { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
}};

const std::vector<VkVertexInputAttributeDescription> attributes = {
    { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)  },
    { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)        },
    { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
    { 3, 0, VK_FORMAT_R32_UINT,      offsetof(Vertex, tex_index) },
};
```

### Шаг 2. Instance, устройство, свапчейн

```cpp
vks::Instance instance = vks::create_instance();
// ... проверка instance.handle != VK_NULL_HANDLE

rgfw::init();
RGFW_window* win = rgfw::createWindow(800, 600, "triangle");

VkSurfaceKHR surface = VK_NULL_HANDLE;
rgfw::createSurface(win, instance.handle, &surface);

vks::Device device = vks::create_device(instance, surface);
vks::Swapchain swapchain = vks::create_swapchain(device, surface, 800, 600);
```

### Шаг 3. Дескрипторы: layout, пул, UBO

```cpp
const std::vector<VkDescriptorSetLayoutBinding> desc_bindings = {
    // UBO FrameData (set 0, binding 0)
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
      VK_SHADER_STAGE_VERTEX_BIT, nullptr },
    // textures[16] (set 0, binding 1)
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16,
      VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
};

vks::DescriptorSetLayout desc_layout = vks::create_descriptor_layout(device, desc_bindings);

const std::vector<VkDescriptorPoolSize> pool_sizes = {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
};
vks::DescriptorPool pool = vks::create_descriptor_pool(device, pool_sizes, 2);
std::vector<VkDescriptorSet> sets = vks::allocate_descriptor_sets(device, pool, desc_layout, 1);
```

### Шаг 4. Буферы: UBO + вершинный буфер

```cpp
struct FrameData {
    vks::Mat4 view;
    vks::Mat4 proj;
    vks::Mat4 view_proj;
    vks::Vec3 camera_pos;
    float     _pad0;
    vks::Vec3 sun_dir;
    float     _pad1;
    float     time;
    float     _pad2;
    float     _pad3;
    float     _pad4;
};

vks::Buffer ubo = vks::create_buffer(device, sizeof(FrameData),
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, /*host_visible=*/true);

FrameData frame_data{};
frame_data.projection = vks::ortho_projection(800.0f, 600.0f);
frame_data.screen_size = { 800.0f, 600.0f };
vks::update_buffer(device, ubo, &frame_data, sizeof(FrameData));
vks::write_descriptor_buffer(device, sets[0], /*binding=*/0, ubo.handle, 0, sizeof(FrameData));

// Треугольник в пиксельных координатах (origin в левом верхнем углу, y вниз).
const std::vector<Vertex> triangle = {
    {{ 400.0f, 100.0f }, { 0, 0 }, { 1, 0, 0, 1 }, 0},   // верх
    {{ 150.0f, 500.0f }, { 0, 0 }, { 0, 1, 0, 1 }, 0},   // левый низ
    {{ 650.0f, 500.0f }, { 0, 0 }, { 0, 0, 1, 1 }, 0},   // правый низ
};

vks::Buffer vbo = vks::create_buffer(device, triangle.size() * sizeof(Vertex),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, /*host_visible=*/true);
vks::update_buffer(device, vbo, triangle.data(), triangle.size() * sizeof(Vertex));
```

### Шаг 5. Текстура (обязательна: шейдер семплирует `textures[0]`)

Белая текстура 1×1. Цвет задаёт вершинный `color`, который умножается на текстуру.

```cpp
uint32_t white_pixel = 0xFFFFFFFFu;   // R8G8B8A8
vks::Texture white = vks::create_texture(device, 1, 1, &white_pixel);
std::array<VkImageView, 1> white_views = { white.view };
vks::write_descriptor_textures(device, sets[0], /*binding=*/1, white_views, white.sampler);
```

### Шаг 6. Пайплайн

```cpp
vks::PipelineConfig config;
config.bindings              = bindings;
config.attributes            = attributes;
config.descriptor_set_layout = desc_layout.handle;
config.alpha_blend           = false;

vks::FramePipeline pipeline = vks::create_frame_pipeline(device, swapchain, "shaders", config);
```

> **Внимание на winding:** пайплайн режет back-face, front-face — по часовой
> стрелке. Вершины треугольника выше заданы по часовой стрелке в экранных
> координатах с y вниз. Если сцена «невидима» — переверните порядок вершин.

### Шаг 7. Главный цикл

```cpp
for (;;) {
    RGFW_event ev;
    if (RGFW_window_checkEvent(win, &ev)) continue;   // обработка событий
    if (RGFW_window_shouldClose(win)) break;

    VkCommandBuffer cmd = vks::begin_frame(device, swapchain, pipeline);
    if (cmd == VK_NULL_HANDLE) continue;              // OUT_OF_DATE: пересоздано

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
    VkBuffer vertex_buffers[] = { vbo.handle };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.layout, 0, 1, &sets[0], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);                       // 3 вершины, 1 инстанс

    if (vks::end_frame(device, swapchain, pipeline, cmd) != VK_SUCCESS) break;
}
```

### Шаг 8. Уничтожение (обратный порядок)

```cpp
vks::wait_device_idle(device);
vks::destroy_buffer(device, vbo);
vks::destroy_buffer(device, ubo);
vks::destroy_texture(device, white);
vks::destroy_frame_pipeline(device, pipeline);
vks::destroy_descriptor_pool(device, pool);
vks::destroy_descriptor_layout(device, desc_layout);
vks::destroy_swapchain(device, swapchain);
vks::destroy_device(device);
vkDestroySurfaceKHR(instance.handle, surface, nullptr);   // surface — наша
vks::destroy_instance(instance);
rgfw::destroyWindow(win);
```

---

## Гайд: как нарисовать прямоугольник

Прямоугольник — это **два треугольника**, образующих квадрат со сторонами,
параллельными осям. Вершины идут по часовой стрелке:

```
1.0  A──────────B
     │ ╲        │
     │   ╲      │
     │     ╲    │   A = (x0, y0)          D = (x0, y1)
     │       ╲  │   B = (x1, y0)          C = (x1, y1)
0.0  D──────────C
```

Порядок (clockwise): `A -> B -> C`, затем `A -> C -> D`.

```cpp
struct Vertex { vks::Vec2 position; vks::Vec2 uv; vks::Vec4 color; uint32_t tex_index; };

// Заполняет scratch-вектор шестью вершинами (два треугольника).
// pos — левый верхний угол, size — ширина/высота в пикселях.
void push_rectangle(std::vector<Vertex>& out,
                    vks::Vec2 pos, vks::Vec2 size,
                    vks::Rect uv, vks::Color tint, uint32_t tex_index) {
    const vks::Vec2 min = pos;
    const vks::Vec2 max = { pos.x + size.x, pos.y + size.y };
    const vks::Vec4 color = { tint.r, tint.g, tint.b, tint.a };

    out.push_back({ min,              { uv.min.x, uv.min.y }, color, tex_index }); // A
    out.push_back({ { max.x, min.y }, { uv.max.x, uv.min.y }, color, tex_index }); // B
    out.push_back({ max,              { uv.max.x, uv.max.y }, color, tex_index }); // C
    out.push_back({ min,              { uv.min.x, uv.min.y }, color, tex_index }); // A
    out.push_back({ max,              { uv.max.x, uv.max.y }, color, tex_index }); // C
    out.push_back({ { min.x, max.y }, { uv.min.x, uv.max.y }, color, tex_index }); // D
}
```

Затем используйте тот же каркас, что и в гайде про треугольник, но:

1. Вершинный буфер наполните результатом `push_rectangle`.
2. В `vkCmdDraw` укажите `vertex_count = 6` (шесть вершин на один прямоугольник),
   либо `vertex_count = 6 * N` для `N` прямоугольников.
3. Если хотите сплошную заливку без картинки — белая текстура 1×1 + цвет в
   `tint`.

Пример: красный прямоугольник 100×200 с верхним левым углом в (300, 300):

```cpp
push_rectangle(vertices,
               { 300.0f, 300.0f },   // pos
               { 100.0f, 200.0f },   // size
               { {0, 0}, {1, 1} },   // uv — вся текстура
               { 1, 0, 0, 1 },       // красный
               0);                   // индекс белой текстуры

vks::update_buffer(device, vbo, vertices.data(), vertices.size() * sizeof(Vertex));
// ... begin_frame / bind / draw c count = vertices.size() ...
```

---

## Гайд: кастомные прямоугольники

«Кастомный прямоугольник» = прямоугольник с **произвольными UV и тинтом** —
это даёт спрайты из атласа, полупрозрачность, повёрнутые/масштабируемые плитки.

### Спрайт из атласа

Атлас — одна текстура с несколькими картинками. Индекс нужной картинки —
UV-прямоугольник в нормализованных координатах `[0,1]`.

Допустим, атлас 2×2 клетки, верхняя левая клетка занимает UV `(0,0)-(0.5,0.5)`:

```cpp
// текстура атласа: 64x64
uint8_t atlas_pixels[64*64*4] = { /* ... */ };
vks::Texture atlas = vks::create_texture(device, 64, 64, atlas_pixels);
// сет должен указывать на atlas.view/atlas.sampler (см. write_descriptor_textures)

// Размер клетки в атласе, в нормализованных UV.
const float tile = 0.5f;

vks::Rect uv_of_cell_0_0 = { { 0.0f,       0.0f       },   // min
                             { tile,       tile        } }; // max

// Нарисовать клетку как прямоугольник 140x140 с верхним левым углом в (30, 30)
push_rectangle(vertices, { 30, 30 }, { 140, 140 }, uv_of_cell_0_0,
               { 1, 1, 1, 1 }, /*tex_index=*/ 1);   // 1 — индекс атласа в textures[]
```

Любая клетка `(cx, cy)` в сетке атласа `cols × rows`:

```cpp
vks::Rect cell_uv(uint32_t cx, uint32_t cy, uint32_t cols, uint32_t rows) {
    const float u0 = float(cx) / cols, u1 = float(cx + 1) / cols;
    const float v0 = float(cy) / rows, v1 = float(cy + 1) / rows;
    return { { u0, v0 }, { u1, v1 } };   // v растёт вниз — подходит для атласов RGFW-style
}
```

### Полупрозрачный прямоугольник

`alpha_blend = true` у пайплайна, тогда можно рисовать оверлеи:

```cpp
config.alpha_blend = true;   // в PipelineConfig

// Чёрная полупрозрачная «вуаль» поверх сцены:
push_rectangle(vertices, { 0, 0 }, { 800, 600 },
               { {0,0}, {1,1} }, { 0, 0, 0, 0.5f }, 0);
```

### Несколько прямоугольников за один кадр (батчинг)

Все прямоугольники кладут вершины в один буфер и рисуются одним `vkCmdDraw` —
это самый быстрый путь:

```cpp
std::vector<Vertex> batch;
push_rectangle(batch, { 30, 30 },  { 140, 140 }, uv_of_cell_0_0, white(), 1);
push_rectangle(batch, { 30, 200 }, { 140, 140 }, uv_of_cell_0_1, white(), 1);
push_rectangle(batch, { 230, 120 },{ 240, 200 }, full_uv(),     {0,0,0,0.5f}, 0);

vks::update_buffer(device, vbo, batch.data(), batch.size() * sizeof(Vertex));
// ...
vkCmdDraw(cmd, static_cast<uint32_t>(batch.size()), 1, 0, 0);
```

> Ограничение: с данным буфером и с `vkCmdDraw` без first_vertex позиция
> прямоугольника — это именно его координаты. Для переноса/поворота с
> общим буфером можно либо пересчитывать вершины на CPU, либо держать
> отдельный буфер на группу и менять `firstVertex`.

### Кастомный вершинный формат / свои шейдеры

Слой не завязан на «квадовую» раскладку. Передайте свой layout в
`PipelineConfig`, свои `.spv` в `shader_dir` — и рисуйте любые примитивы
(линии, полосы, инстансы и т.п.):

```cpp
struct MyVertex { vks::Vec2 pos; float scale; uint32_t flags; };

const std::vector<VkVertexInputBindingDescription> my_bindings = {{
    { 0, sizeof(MyVertex), VK_VERTEX_INPUT_RATE_VERTEX },
}};
const std::vector<VkVertexInputAttributeDescription> my_attributes = {
    { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
    { 1, 0, VK_FORMAT_R32_SFLOAT,    offsetof(MyVertex, scale) },
    { 2, 0, VK_FORMAT_R32_UINT,      offsetof(MyVertex, flags) },
};

config.bindings = my_bindings;
config.attributes = my_attributes;
config.backface_cull = false;   // если нужны двусторонние примитивы
```