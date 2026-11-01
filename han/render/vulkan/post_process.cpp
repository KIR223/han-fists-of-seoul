#include "post_process.h"

#include <algorithm>
#include <cstring>

#include "han/render/generated/bloom_down_frag.h"
#include "han/render/generated/bloom_up_frag.h"
#include "han/render/generated/post_vert.h"
#include "han/render/generated/taa_frag.h"
#include "han/render/generated/tonemap_frag.h"

namespace han::render {
namespace {

constexpr VkFormat kBloomFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // как у HDR-цели сцены и истории TAA

// Push constants bloom (fragment): спуск использует все пять чисел, подъём — первые три (texel, radius).
struct BloomPush {
    float texel[2];
    float a;  // спуск: порог; подъём: радиус
    float knee;
    float prefilter;
};
struct TonemapPush {
    float exposure, contrast, saturation, bloomIntensity;
};
struct TaaPush {
    float reproject[16];
    float feedback;
    float historyValid;
};

void imageBarrier(VkCommandBuffer cmd, VkImage image, std::uint32_t mipCount, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
                  VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// Общий барьер между проходами bloom: запись цвета одного прохода должна быть видна чтению и дозаписи следующего.
void passBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// Один полноэкранный проход: цель, конвейер, набор с источником, push constants.
void fullscreenPass(VkCommandBuffer cmd, VkImageView target, VkImageLayout targetLayout, VkAttachmentLoadOp loadOp, VkExtent2D extent,
                    VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet set, const void* push, std::uint32_t pushSize) {
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target;
    color.imageLayout = targetLayout;
    color.loadOp = loadOp;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, extent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &info);
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

}  // namespace

bool PostProcess::init(const Context& context, VkFormat swapchainFormat, std::string& err) {
    ctx_ = context;
    using vk::fail;

    // Тонмаппинг читает две картинки: сцену (binding 0) и свечение (binding 1).
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    lci.pBindings = bindings.data();
    VkResult r = vkCreateDescriptorSetLayout(ctx_.device, &lci, nullptr, &tonemapSetLayout_);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDescriptorSetLayout (тонмаппинг)", r);

    // TAA читает три картинки: сцену (0), историю (1) и глубину (2).
    std::array<VkDescriptorSetLayoutBinding, 3> taaBindings{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        taaBindings[i].binding = i;
        taaBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        taaBindings[i].descriptorCount = 1;
        taaBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo tlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    tlci.bindingCount = static_cast<std::uint32_t>(taaBindings.size());
    tlci.pBindings = taaBindings.data();
    r = vkCreateDescriptorSetLayout(ctx_.device, &tlci, nullptr, &taaSetLayout_);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDescriptorSetLayout (TAA)", r);

    VkSamplerCreateInfo nsi{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    nsi.magFilter = nsi.minFilter = VK_FILTER_NEAREST;
    nsi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    nsi.addressModeU = nsi.addressModeV = nsi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    r = vkCreateSampler(ctx_.device, &nsi, nullptr, &nearestSampler_);
    if (r != VK_SUCCESS) return fail(err, "vkCreateSampler (глубина)", r);

    const VkPushConstantRange taaPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TaaPush)};
    VkPipelineLayoutCreateInfo tpi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    tpi.setLayoutCount = 1;
    tpi.pSetLayouts = &taaSetLayout_;
    tpi.pushConstantRangeCount = 1;
    tpi.pPushConstantRanges = &taaPush;
    r = vkCreatePipelineLayout(ctx_.device, &tpi, nullptr, &taaLayout_);
    if (r != VK_SUCCESS) return fail(err, "vkCreatePipelineLayout (TAA)", r);

    const VkPushConstantRange bloomPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPush)};
    VkPipelineLayoutCreateInfo bli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    bli.setLayoutCount = 1;
    bli.pSetLayouts = &ctx_.samplerLayout;
    bli.pushConstantRangeCount = 1;
    bli.pPushConstantRanges = &bloomPush;
    r = vkCreatePipelineLayout(ctx_.device, &bli, nullptr, &bloomLayout_);
    if (r != VK_SUCCESS) return fail(err, "vkCreatePipelineLayout (bloom)", r);

    const VkPushConstantRange tonemapPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TonemapPush)};
    VkPipelineLayoutCreateInfo tli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    tli.setLayoutCount = 1;
    tli.pSetLayouts = &tonemapSetLayout_;
    tli.pushConstantRangeCount = 1;
    tli.pPushConstantRanges = &tonemapPush;
    r = vkCreatePipelineLayout(ctx_.device, &tli, nullptr, &tonemapLayout_);
    if (r != VK_SUCCESS) return fail(err, "vkCreatePipelineLayout (тонмаппинг)", r);

    vk::PipelineDesc down;
    down.vert = shaders::k_post_vert;
    down.vertBytes = sizeof(shaders::k_post_vert);
    down.frag = shaders::k_bloom_down_frag;
    down.fragBytes = sizeof(shaders::k_bloom_down_frag);
    down.layout = bloomLayout_;
    down.colorFormat = kBloomFormat;
    r = vk::buildPipeline(ctx_.device, down, bloomDown_);
    if (r != VK_SUCCESS) return fail(err, "конвейер bloom (спуск)", r);

    vk::PipelineDesc up = down;
    up.frag = shaders::k_bloom_up_frag;
    up.fragBytes = sizeof(shaders::k_bloom_up_frag);
    up.additiveBlend = true;
    r = vk::buildPipeline(ctx_.device, up, bloomUp_);
    if (r != VK_SUCCESS) return fail(err, "конвейер bloom (подъём)", r);

    vk::PipelineDesc resolve;
    resolve.vert = shaders::k_post_vert;
    resolve.vertBytes = sizeof(shaders::k_post_vert);
    resolve.frag = shaders::k_taa_frag;
    resolve.fragBytes = sizeof(shaders::k_taa_frag);
    resolve.layout = taaLayout_;
    resolve.colorFormat = kSceneFormat;
    r = vk::buildPipeline(ctx_.device, resolve, taa_);
    if (r != VK_SUCCESS) return fail(err, "конвейер TAA", r);

    vk::PipelineDesc tone;
    tone.vert = shaders::k_post_vert;
    tone.vertBytes = sizeof(shaders::k_post_vert);
    tone.frag = shaders::k_tonemap_frag;
    tone.fragBytes = sizeof(shaders::k_tonemap_frag);
    tone.layout = tonemapLayout_;
    tone.colorFormat = swapchainFormat;
    r = vk::buildPipeline(ctx_.device, tone, tonemap_);
    return r == VK_SUCCESS ? true : fail(err, "конвейер тонмаппинга", r);
}

void PostProcess::releaseTargets() {
    if (!ctx_.device) return;
    const auto freeSet = [&](VkDescriptorSet& set) {
        if (set) vkFreeDescriptorSets(ctx_.device, ctx_.descriptorPool, 1, &set);
        set = VK_NULL_HANDLE;
    };
    for (VkDescriptorSet& set : sceneSet_) freeSet(set);
    for (VkDescriptorSet& set : tonemapSet_) freeSet(set);
    for (VkDescriptorSet& set : taaSet_) freeSet(set);
    for (vk::Image& image : resolve_) vk::destroyImage(ctx_.device, image);
    for (std::uint32_t i = 0; i < kMaxBloomMips; ++i) {
        freeSet(mipSet_[i]);
        if (mipView_[i]) vkDestroyImageView(ctx_.device, mipView_[i], nullptr);
        mipView_[i] = VK_NULL_HANDLE;
    }
    vk::destroyImage(ctx_.device, bloom_);
    bloomMips_ = 0;
    hdrImage_ = VK_NULL_HANDLE;
}

bool PostProcess::resize(VkExtent2D extent, VkImage hdrImage, VkImageView hdrView, VkImageView depthView, std::string& err) {
    using vk::fail;
    releaseTargets();
    hdrImage_ = hdrImage;

    // Bloom считается в половинном разрешении; уровней не больше шести и не меньше, чем помещается в картинку.
    const std::uint32_t baseW = std::max(1u, extent.width / 2), baseH = std::max(1u, extent.height / 2);
    std::uint32_t levels = 1;
    while (levels < kMaxBloomMips && (baseW >> levels) >= 1 && (baseH >> levels) >= 1) ++levels;
    bloomMips_ = levels;
    if (!vk::createImage2D(ctx_.device, ctx_.physical, baseW, baseH, bloomMips_, kBloomFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, bloom_, err))
        return false;

    const auto allocateSet = [&](VkDescriptorSetLayout layout, VkDescriptorSet& set) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = ctx_.descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        return vkAllocateDescriptorSets(ctx_.device, &ai, &set);
    };
    const auto writeSampler = [&](VkDescriptorSet set, std::uint32_t binding, VkImageView view, VkImageLayout layout) {
        VkDescriptorImageInfo info{ctx_.sampler, view, layout};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &info;
        vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
    };

    for (std::uint32_t i = 0; i < bloomMips_; ++i) {
        mipExtent_[i] = {std::max(1u, baseW >> i), std::max(1u, baseH >> i)};
        if (!vk::createMipView(ctx_.device, bloom_.image, kBloomFormat, VK_IMAGE_ASPECT_COLOR_BIT, i, mipView_[i], err)) return false;
        const VkResult r = allocateSet(ctx_.samplerLayout, mipSet_[i]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateDescriptorSets (bloom)", r);
        writeSampler(mipSet_[i], 0, mipView_[i], VK_IMAGE_LAYOUT_GENERAL);
    }

    // История TAA: два изображения; сначала оба в раскладке UNDEFINED, первый record переведёт их в «чтение».
    for (vk::Image& image : resolve_)
        if (!vk::createImage2D(ctx_.device, ctx_.physical, extent.width, extent.height, 1, kSceneFormat,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, image, err))
            return false;
    taaCur_ = 0;
    needsInit_ = true;

    // Источники сцены: 0 — сырая сцена, 1 и 2 — результаты TAA. К каждому свой набор для bloom и для тонмаппинга.
    const std::array<VkImageView, 3> sceneViews{hdrView, resolve_[0].view, resolve_[1].view};
    VkResult r = VK_SUCCESS;
    for (std::size_t k = 0; k < sceneViews.size(); ++k) {
        r = allocateSet(ctx_.samplerLayout, sceneSet_[k]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateDescriptorSets (сцена)", r);
        writeSampler(sceneSet_[k], 0, sceneViews[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        r = allocateSet(tonemapSetLayout_, tonemapSet_[k]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateDescriptorSets (тонмаппинг)", r);
        writeSampler(tonemapSet_[k], 0, sceneViews[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        writeSampler(tonemapSet_[k], 1, mipView_[0], VK_IMAGE_LAYOUT_GENERAL);
    }
    // TAA: taaSet_[k] пишет в resolve_[k]; историей служит resolve_[1 − k]. Глубину читаем без интерполяции.
    for (std::size_t k = 0; k < 2; ++k) {
        r = allocateSet(taaSetLayout_, taaSet_[k]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateDescriptorSets (TAA)", r);
        writeSampler(taaSet_[k], 0, hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        writeSampler(taaSet_[k], 1, resolve_[1 - k].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageInfo depthInfo{nearestSampler_, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = taaSet_[k];
        w.dstBinding = 2;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
    }
    return true;
}

void PostProcess::record(VkCommandBuffer cmd, VkImageView swapchainView, VkExtent2D extent, const PostParams& p) {
    if (needsInit_) {  // первые кадры: оба изображения истории приводим в раскладку «чтение», как они остаются после каждого кадра
        for (const vk::Image& image : resolve_)
            imageBarrier(cmd, image.image, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        needsInit_ = false;
    }
    // Сцена: из цели рисования — в источник для чтения.
    imageBarrier(cmd, hdrImage_, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // TAA: накопление истории. Результат кладём в resolve_[taaCur_]; дальше bloom и тонмаппинг читают уже его.
    std::size_t source = 0;
    if (p.taa) {
        const int cur = taaCur_;
        // Это изображение две кадра назад было результатом, а в прошлом кадре — историей (чтение): ждём конца чтения, прежнее содержимое не нужно.
        imageBarrier(cmd, resolve_[static_cast<std::size_t>(cur)].image, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        TaaPush taaPush{};
        std::memcpy(taaPush.reproject, p.reproject, sizeof(taaPush.reproject));
        taaPush.feedback = p.taaFeedback;
        taaPush.historyValid = p.historyValid ? 1.0f : 0.0f;
        fullscreenPass(cmd, resolve_[static_cast<std::size_t>(cur)].view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                       extent, taa_, taaLayout_, taaSet_[static_cast<std::size_t>(cur)], &taaPush, sizeof(taaPush));
        imageBarrier(cmd, resolve_[static_cast<std::size_t>(cur)].image, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        source = 1 + static_cast<std::size_t>(cur);
        taaCur_ = 1 - cur;
    }
    // Bloom целиком переписывается каждый кадр: прежнее содержимое не нужно; прошлый кадр мог ещё читать его в тонмаппинге.
    imageBarrier(cmd, bloom_.image, bloomMips_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Спуск: сцена → уровень 0 (с порогом), затем каждый следующий из предыдущего.
    BloomPush push{};
    push.texel[0] = 1.0f / static_cast<float>(extent.width);
    push.texel[1] = 1.0f / static_cast<float>(extent.height);
    push.a = p.bloomThreshold;
    push.knee = std::max(p.bloomKnee, 1e-3f);
    push.prefilter = 1.0f;
    fullscreenPass(cmd, mipView_[0], VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_LOAD_OP_DONT_CARE, mipExtent_[0], bloomDown_, bloomLayout_, sceneSet_[source], &push,
                   sizeof(push));
    for (std::uint32_t i = 1; i < bloomMips_; ++i) {
        passBarrier(cmd);
        push.texel[0] = 1.0f / static_cast<float>(mipExtent_[i - 1].width);
        push.texel[1] = 1.0f / static_cast<float>(mipExtent_[i - 1].height);
        push.prefilter = 0.0f;
        fullscreenPass(cmd, mipView_[i], VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_LOAD_OP_DONT_CARE, mipExtent_[i], bloomDown_, bloomLayout_,
                       mipSet_[i - 1], &push, sizeof(push));
    }
    // Подъём: нижний уровень аддитивно прибавляется к верхнему, пока всё не соберётся в уровне 0.
    for (std::uint32_t i = bloomMips_ - 1; i >= 1; --i) {
        passBarrier(cmd);
        push.texel[0] = 1.0f / static_cast<float>(mipExtent_[i].width);
        push.texel[1] = 1.0f / static_cast<float>(mipExtent_[i].height);
        push.a = p.bloomRadius;
        fullscreenPass(cmd, mipView_[i - 1], VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_LOAD_OP_LOAD, mipExtent_[i - 1], bloomUp_, bloomLayout_, mipSet_[i],
                       &push, sizeof(push));
    }
    passBarrier(cmd);

    // Тонмаппинг: сцена + bloom → экран.
    const TonemapPush tone{p.exposure, p.contrast, p.saturation, p.bloomIntensity};
    fullscreenPass(cmd, swapchainView, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_LOAD_OP_DONT_CARE, extent, tonemap_, tonemapLayout_,
                   tonemapSet_[source], &tone, sizeof(tone));
}

void PostProcess::destroy() {
    if (!ctx_.device) return;
    releaseTargets();
    if (bloomDown_) vkDestroyPipeline(ctx_.device, bloomDown_, nullptr);
    if (bloomUp_) vkDestroyPipeline(ctx_.device, bloomUp_, nullptr);
    if (tonemap_) vkDestroyPipeline(ctx_.device, tonemap_, nullptr);
    if (taa_) vkDestroyPipeline(ctx_.device, taa_, nullptr);
    if (taaLayout_) vkDestroyPipelineLayout(ctx_.device, taaLayout_, nullptr);
    if (taaSetLayout_) vkDestroyDescriptorSetLayout(ctx_.device, taaSetLayout_, nullptr);
    if (nearestSampler_) vkDestroySampler(ctx_.device, nearestSampler_, nullptr);
    if (bloomLayout_) vkDestroyPipelineLayout(ctx_.device, bloomLayout_, nullptr);
    if (tonemapLayout_) vkDestroyPipelineLayout(ctx_.device, tonemapLayout_, nullptr);
    if (tonemapSetLayout_) vkDestroyDescriptorSetLayout(ctx_.device, tonemapSetLayout_, nullptr);
    bloomDown_ = bloomUp_ = tonemap_ = taa_ = VK_NULL_HANDLE;
    taaLayout_ = VK_NULL_HANDLE;
    taaSetLayout_ = VK_NULL_HANDLE;
    nearestSampler_ = VK_NULL_HANDLE;
    bloomLayout_ = tonemapLayout_ = VK_NULL_HANDLE;
    tonemapSetLayout_ = VK_NULL_HANDLE;
}

}  // namespace han::render
