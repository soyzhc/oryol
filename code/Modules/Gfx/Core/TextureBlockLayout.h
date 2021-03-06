#pragma once
//------------------------------------------------------------------------------
/**
    @class Oryol::TextureBlockLayout
    @ingroup Gfx
    @brief describes the name, type and bindSlot of a group of related textures
*/
#include "Gfx/Core/Enums.h"
#include "Core/String/StringAtom.h"
#include "Core/Containers/StaticArray.h"
#include "Gfx/Core/GfxConfig.h"

namespace Oryol {

class TextureBlockLayout {
public:
    /// a TextureBlockLayout component describes a single texture entry
    class Component {
    public:
        /// default constructor
        Component();
        /// construct from name, type and bindSlot
        Component(const StringAtom& name, TextureType::Code type, int bindSlot);
        /// return true if the component is valid
        bool IsValid() const;

        StringAtom Name;            ///< the (shader-bind) name of the texture entry 
        TextureType::Code Type;     ///< the texture type (2d, cube, ...)
        int BindSlot;             ///< the texture bind slot
    };

    /// constructor
    TextureBlockLayout();

    /// clear the texture layout
    void Clear();
    /// return true if the layout is empty
    bool Empty() const;

    /// add a component to the layout
    TextureBlockLayout& Add(const Component& comp);
    /// add a component to the layout
    TextureBlockLayout& Add(const StringAtom& name, TextureType::Code type, int bindSlot);
    /// get number of components in the layout
    int NumComponents() const;
    /// find component index with matching bind slot, InvalidIndex if not match
    int ComponentIndexForBindSlot(int bindSlot) const;
    /// get component at index
    const Component& ComponentAt(int index) const;

private:
    int numComps;
    StaticArray<Component, GfxConfig::MaxNumTextureBlockLayoutComponents> comps;
};

//------------------------------------------------------------------------------
inline
TextureBlockLayout::Component::Component() :
Type(TextureType::InvalidTextureType),
BindSlot(InvalidIndex) {
    // empty
}

//------------------------------------------------------------------------------
inline
TextureBlockLayout::Component::Component(const StringAtom& name, TextureType::Code type, int bindSlot) :
Name(name),
Type(type),
BindSlot(bindSlot) {
    // empty
}

//------------------------------------------------------------------------------
inline bool
TextureBlockLayout::Component::IsValid() const {
    return this->Name.IsValid();
}

//------------------------------------------------------------------------------
inline bool
TextureBlockLayout::Empty() const {
    return 0 == this->numComps;
}

//------------------------------------------------------------------------------
inline int
TextureBlockLayout::NumComponents() const {
    return this->numComps;
}

//------------------------------------------------------------------------------
inline const TextureBlockLayout::Component&
TextureBlockLayout::ComponentAt(int index) const {
    return this->comps[index];
}

} // namespace Oryol
