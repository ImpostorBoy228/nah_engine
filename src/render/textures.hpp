#pragma once

#include "render/image.hpp"
#include "render/vulkan_casino_777.hpp"
#include "paths.hpp"

// texture management: a fixed pool of gpu textures behind small stable handles.
// the same path loads once, slots are refcounted and recycled through a freelist.
// fixed arrays + freelist on purpose, no stl anywhere in here.
namespace prim {

    static const u32 MAX_TEXTURES = 128;

    // index 0 is the reserved white texture, user ids always have index >= 1.
    // gen catches handles that outlive the slot they point at (use after free).
    struct TextureId {
        u32 index;
        u32 gen;
    };

    inline bool isValid(TextureId id) {
        return id.index != 0 && id.index < MAX_TEXTURES;
    }

    // pack a handle into one int64 so the lua ffi can pass it around as a number
    inline long long packId(TextureId id) {
        return ((long long)id.gen << 32) | (long long)id.index;
    }

    inline TextureId unpackId(long long handle) {
        TextureId id;
        id.index = (u32)(handle & 0xFFFFFFFFll);
        id.gen = (u32)(((unsigned long long)handle) >> 32);
        return id;
    }

    class TextureCache {
    public:
        const casino::Device* dev = NULL;
        casino::Texture slots[MAX_TEXTURES];
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;

        TextureCache() = default;
        ~TextureCache() { shutdown(); }
        TextureCache(const TextureCache&) = delete;
        TextureCache& operator=(const TextureCache&) = delete;

        bool init(const casino::Device& device); // BLACKBOX
        void shutdown();

        TextureId acquire(const char* path); // BLACKBOX
        void release(TextureId id);
        const casino::Texture& get(TextureId id) const;
        const casino::Texture& white() const { return slots[0]; }

    private:
        u32 refs[MAX_TEXTURES];
        u32 gens[MAX_TEXTURES];
        u32 free_stack[MAX_TEXTURES];
        u32 free_count = 0;
        char* paths = NULL; // MAX_TEXTURES * PATH_MAX, flat

        char* pathAt(u32 i) { return paths + (size_t)i * PATH_MAX; }
        u32 findByPath(const char* path);
    };

    inline bool TextureCache::init(const casino::Device& device) { // BLACKBOX
        shutdown();
        dev = &device;
        VkDevice d = device.device;

        // one combined-image-sampler binding, fragment stage
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo linfo = {};
        linfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        linfo.bindingCount = 1;
        linfo.pBindings = &binding;
        if (!casino::check(vkCreateDescriptorSetLayout(d, &linfo, NULL, &set_layout), "set layout"))
            return false;

        VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES };
        VkDescriptorPoolCreateInfo pinfo = {};
        pinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pinfo.maxSets = MAX_TEXTURES;
        pinfo.poolSizeCount = 1;
        pinfo.pPoolSizes = &pool_size;
        if (!casino::check(vkCreateDescriptorPool(d, &pinfo, NULL, &pool), "desc pool"))
            return false;

        paths = (char*)malloc((size_t)MAX_TEXTURES * PATH_MAX);
        if (paths == NULL)
            return false;

        for (u32 i = 0; i < MAX_TEXTURES; i++) {
            refs[i] = 0;
            gens[i] = 0;
        }
        // white takes slot 0 and is never recycled, the freelist holds 1..127
        free_count = 0;
        for (u32 i = MAX_TEXTURES; i > 1; i--)
            free_stack[free_count++] = i - 1;

        // white 1x1 so untextured prims sample a solid texel
        uint8_t px[4] = { 255, 255, 255, 255 };
        if (!slots[0].create(device, px, 1, 1, pool, set_layout))
            return false;
        refs[0] = 1;
        return true;
    }

    inline void TextureCache::shutdown() {
        if (dev && dev->device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(dev->device);
        for (u32 i = 0; i < MAX_TEXTURES; i++) {
            slots[i].destroy();
            refs[i] = 0;
        }
        if (pool) vkDestroyDescriptorPool(dev ? dev->device : VK_NULL_HANDLE, pool, NULL);
        if (set_layout) vkDestroyDescriptorSetLayout(dev ? dev->device : VK_NULL_HANDLE, set_layout, NULL);
        pool = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        free(paths);
        paths = NULL;
        free_count = 0;
        dev = NULL;
    }


    inline u32 TextureCache::findByPath(const char* path) {
        for (u32 i = 1; i < MAX_TEXTURES; i++) {
            if (refs[i] > 0 && strcmp(pathAt(i), path) == 0)
                return i;
        }
        return 0;
    }

    inline TextureId TextureCache::acquire(const char* path) { // BLACKBOX
        TextureId bad = { 0, 0 };
        if (dev == NULL || path == NULL)
            return bad;

        char resolved[PATH_MAX];
        const char* key = paths::resolve(path, resolved, sizeof(resolved));

        u32 hit = findByPath(key);
        if (hit != 0) {
            refs[hit] += 1;
            TextureId id = { hit, gens[hit] };
            return id;
        }

        if (free_count == 0) {
            fprintf(stderr, "prim: texture pool full (%u), cannot load %s\n", MAX_TEXTURES, key);
            return bad;
        }
        u32 slot = free_stack[--free_count];

        img::Image image = {};
        if (!img::loadPng(key, &image) || image.pixels == NULL) {
            free_stack[free_count++] = slot;
            return bad;
        }
        if (!slots[slot].create(*dev, image.pixels, image.width, image.height, pool, set_layout)) {
            img::freeImage(&image);
            free_stack[free_count++] = slot;
            return bad;
        }
        img::freeImage(&image);

        snprintf(pathAt(slot), PATH_MAX, "%s", key);
        refs[slot] = 1;
        gens[slot] += 1;
        TextureId id = { slot, gens[slot] };
        return id;
    }

    inline void TextureCache::release(TextureId id) {
        if (!isValid(id) || gens[id.index] != id.gen || refs[id.index] == 0)
            return;
        refs[id.index] -= 1;
        if (refs[id.index] > 0)
            return;
        slots[id.index].destroy();
        pathAt(id.index)[0] = '\0';
        // gen bump happens on the next acquire of this slot, refs already guard it
        free_stack[free_count++] = id.index;
    }

    inline const casino::Texture& TextureCache::get(TextureId id) const {
        if (isValid(id) && refs[id.index] > 0 && gens[id.index] == id.gen)
            return slots[id.index];
        return slots[0]; // white fallback so a stale handle never crashes a draw
    }

} // namespace prim
