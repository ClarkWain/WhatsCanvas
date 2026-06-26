#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsc {

enum class FontSlant
{
    NORMAL,
    ITALIC,
    OBLIQUE
};

enum class FontSourceType
{
    FILE,
    MEMORY
};

struct FontDescriptor
{
    std::string family;
    int weight = 400;
    FontSlant slant = FontSlant::NORMAL;

    FontDescriptor() = default;
    explicit FontDescriptor(std::string familyName)
        : family(std::move(familyName))
    {
    }
    FontDescriptor(std::string familyName, int fontWeight, FontSlant fontSlant = FontSlant::NORMAL)
        : family(std::move(familyName)),
          weight(fontWeight),
          slant(fontSlant)
    {
    }
};

class FontFace
{
public:
    static FontFace fromFile(FontDescriptor descriptor, std::string path)
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::FILE;
        face.path_ = std::move(path);
        return face;
    }

    static FontFace fromMemory(FontDescriptor descriptor, std::vector<std::uint8_t> bytes)
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::MEMORY;
        face.bytes_ = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
        return face;
    }

    const FontDescriptor &descriptor() const { return descriptor_; }
    const std::string &family() const { return descriptor_.family; }
    int weight() const { return descriptor_.weight; }
    FontSlant slant() const { return descriptor_.slant; }
    FontSourceType sourceType() const { return sourceType_; }
    const std::string &path() const { return path_; }
    const std::vector<std::uint8_t> *bytes() const { return bytes_ ? bytes_.get() : nullptr; }
    bool isValid() const
    {
        return !descriptor_.family.empty()
            && ((sourceType_ == FontSourceType::FILE && !path_.empty())
                || (sourceType_ == FontSourceType::MEMORY && bytes_ && !bytes_->empty()));
    }

private:
    FontDescriptor descriptor_;
    FontSourceType sourceType_ = FontSourceType::FILE;
    std::string path_;
    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
};

class FontFallbackChain
{
public:
    FontFallbackChain() = default;
    explicit FontFallbackChain(std::string primaryFamily)
        : primaryFamily_(std::move(primaryFamily))
    {
    }

    const std::string &primaryFamily() const { return primaryFamily_; }
    void setPrimaryFamily(std::string family) { primaryFamily_ = std::move(family); }

    void addFallbackFamily(std::string family)
    {
        if (family.empty() || family == primaryFamily_ || contains(family)) {
            return;
        }
        fallbackFamilies_.push_back(std::move(family));
    }

    bool contains(const std::string &family) const
    {
        return std::find(fallbackFamilies_.begin(), fallbackFamilies_.end(), family) != fallbackFamilies_.end();
    }

    void clearFallbacks() { fallbackFamilies_.clear(); }
    const std::vector<std::string> &fallbackFamilies() const { return fallbackFamilies_; }

    std::vector<std::string> familiesInResolutionOrder() const
    {
        std::vector<std::string> families;
        if (!primaryFamily_.empty()) {
            families.push_back(primaryFamily_);
        }
        families.insert(families.end(), fallbackFamilies_.begin(), fallbackFamilies_.end());
        return families;
    }

private:
    std::string primaryFamily_;
    std::vector<std::string> fallbackFamilies_;
};

class FontManager
{
public:
    bool registerFontFile(const FontDescriptor &descriptor, const std::string &path)
    {
        return registerFace(FontFace::fromFile(descriptor, path));
    }

    bool registerFontMemory(const FontDescriptor &descriptor, std::vector<std::uint8_t> bytes)
    {
        return registerFace(FontFace::fromMemory(descriptor, std::move(bytes)));
    }

    bool registerFace(FontFace face)
    {
        if (!face.isValid()) {
            return false;
        }

        const std::size_t index = faces_.size();
        familyToFaceIndices_[face.family()].push_back(index);
        fallbackChains_.try_emplace(face.family(), FontFallbackChain(face.family()));
        faces_.push_back(std::move(face));
        return true;
    }

    bool hasFamily(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(family);
        return it != familyToFaceIndices_.end() && !it->second.empty();
    }

    const FontFace *findFirstFace(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(family);
        if (it == familyToFaceIndices_.end() || it->second.empty()) {
            return nullptr;
        }
        return &faces_[it->second.front()];
    }

    std::vector<const FontFace *> findFaces(const std::string &family) const
    {
        std::vector<const FontFace *> result;
        const auto it = familyToFaceIndices_.find(family);
        if (it == familyToFaceIndices_.end()) {
            return result;
        }

        result.reserve(it->second.size());
        for (std::size_t index : it->second) {
            result.push_back(&faces_[index]);
        }
        return result;
    }

    bool addFallbackFamily(const std::string &primaryFamily, const std::string &fallbackFamily)
    {
        if (!hasFamily(primaryFamily) || !hasFamily(fallbackFamily)) {
            return false;
        }

        auto &chain = fallbackChains_[primaryFamily];
        chain.setPrimaryFamily(primaryFamily);
        chain.addFallbackFamily(fallbackFamily);
        return true;
    }

    const FontFallbackChain *fallbackChain(const std::string &family) const
    {
        const auto it = fallbackChains_.find(family);
        return it == fallbackChains_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> resolveFamilies(const std::string &preferredFamily) const
    {
        const auto *chain = fallbackChain(preferredFamily);
        if (chain != nullptr) {
            return chain->familiesInResolutionOrder();
        }
        return preferredFamily.empty() ? std::vector<std::string>() : std::vector<std::string>{preferredFamily};
    }

    const std::vector<FontFace> &faces() const { return faces_; }
    void clear()
    {
        faces_.clear();
        familyToFaceIndices_.clear();
        fallbackChains_.clear();
    }

private:
    std::vector<FontFace> faces_;
    std::unordered_map<std::string, std::vector<std::size_t>> familyToFaceIndices_;
    std::unordered_map<std::string, FontFallbackChain> fallbackChains_;
};

} // namespace wsc
