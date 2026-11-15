/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
namespace FSO { namespace SimAntics { class VM; namespace Utils {
// Restrict only this temporary VM on its owning thread. No VM layout or
// ordinary load semantics change; scope exit also handles preparation errors.
class VMLotDemolitionScope {
    inline static thread_local VM* Current = nullptr;
    VM* Previous;
public:
    explicit VMLotDemolitionScope(VM* vm):Previous(Current){if(vm)Current=vm;}
    ~VMLotDemolitionScope(){Current=Previous;}
    VMLotDemolitionScope(const VMLotDemolitionScope&)=delete;
    VMLotDemolitionScope& operator=(const VMLotDemolitionScope&)=delete;
    static bool Active(VM* vm){return vm&&Current==vm;}
};
}}}
