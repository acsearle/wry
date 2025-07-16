//
//  attic.cpp
//  
//
//  Created by Antony Searle on 1/12/2024.
//




// Given the complexity of minerals etc., can we reasonably simplify
// chemistry down to any scheme that roughly matches real industrial
// processes?  Or should we just have arbitrary IDs and recipes?

// processes:
//
// milling
// chloralkali
// pyrometallurgy
//   - calcination
//   - roasting / pyrolisis
//   - smelting
// electrolysis (AlO)
// leaching, precipitation

enum ELEMENT
: i64 {
    
    ELEMENT_NONE,
    
    ELEMENT_HYDROGEN,
    ELEMENT_HELIUM,
    
    ELEMENT_LITHIUM,
    ELEMENT_BERYLLIUM,
    ELEMENT_BORON,
    ELEMENT_CARBON,
    ELEMENT_NITROGEN,
    ELEMENT_OXYGEN,
    ELEMENT_FLUORINE,
    ELEMENT_NEON,
    
    ELEMENT_SODIUM,
    ELEMENT_MAGNESIUM,
    ELEMENT_ALUMINUM,
    ELEMENT_SILICON,
    ELEMENT_PHOSPHORUS,
    ELEMENT_SULFUR,
    ELEMENT_CHLORINE,
    ELEMENT_ARGON,
    
    ELEMENT_POTASSIUM,
    ELEMENT_CALCIUM,
    ELEMENT_SCANDIUM,
    ELEMENT_TITANIUM,
    ELEMENT_VANADIUM,
    
    ELEMENT_CHROMIUM,
    ELEMENT_MANGANESE,
    ELEMENT_IRON,
    ELEMENT_COBALT,
    ELEMENT_NICKEL,
    ELEMENT_COPPER,
    ELEMENT_ZINC,
    ELEMENT_GALLIUM,
    ELEMENT_GERMANIUM,
    ELEMENT_ARSENIC,
    ELEMENT_SELENIUM,
    ELEMENT_BROMINE,
    ELEMENT_KRYPTON,
    
    ELEMENT_RUBIDIUM,
    ELEMENT_STRONTIUM,
    ELEMENT_YTTRIUM,
    ELEMENT_ZIRCONIUM,
    ELEMENT_NIOBIUM,
    ELEMENT_MOLYBDENUM,
    
    // notable but relatively rare
    SILVER,
    TIN,
    PLATINUM,
    GOLD,
    MERCURY,
    LEAD,
    URANIUM,
};

enum COMPOUND : i64 {
    
    WATER, // H2O
    
    // by crust abundance
    SILICON_DIOXIDE,
    
    // ( source)
    
};



//
//  adl.hpp
//  client
//
//  Created by Antony Searle on 1/9/2024.
//

#ifndef adl_hpp
#define adl_hpp

#include <utility>


// The intent of this file is to provide access points of the form
//
//     foo(x)
//
// which find the correct implementation of foo by ADL even when the calling
// context shadows those names.
//
// To provide customization points for types in closed namespaces (notably std)
// we also may explicitly import a namespace

namespace wry {
    
    namespace orphan {
        
        // Forward declare fallback namespace
        //
        // Types that are defined in namespace that we cannot or should not
        // modify, such as :: and ::std, have their customization points
        // dumped here
        
    } // namespace orphan
    
} // namespace wry

#define MAKE_CUSTOMIZATION_POINT_OBJECT(NAME, NAMESPACE)\
namespace adl {\
namespace _detail {\
struct _##NAME {\
decltype(auto) operator()(auto&&... args) const {\
using namespace NAMESPACE;\
return NAME(FORWARD(args)...);\
}\
};\
}\
inline constexpr _detail::_##NAME NAME;\
}

MAKE_CUSTOMIZATION_POINT_OBJECT(debug, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(hash, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(passivate, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(shade, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(swap, ::std)
MAKE_CUSTOMIZATION_POINT_OBJECT(trace, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(trace_weak, ::wry::orphan)

#endif /* adl_hpp */
