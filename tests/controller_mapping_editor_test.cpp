#include "mihoyo/controller_mapping_editor.hpp"
#include <cassert>
#include <iostream>
using namespace cloudgenshin;
int main() {
    input::ControllerMappingEditor editor;
    using A=input::ControllerMappingEditor::Action;
    const auto defaults=input::DefaultControllerMapping("hk4e");
    editor.open("hk4e",defaults);
    const overlay::Rect bounds{0,0,1280,720};
    input::SwitchControllerState raw;raw.a=true;
    for(int i=0;i<5;++i) assert(editor.update(raw,bounds,false,0,{})==A::None && !editor.targets());
    raw={};editor.update(raw,bounds,false,0,{});
    raw.a=true;assert(editor.update(raw,bounds,false,0,{})==A::None && editor.targets());
    for(int i=0;i<5;++i) assert(editor.update(raw,bounds,false,0,{})==A::None); // No held-A double action.
    raw={};editor.update(raw,bounds,false,0,{});
    assert(editor.activate(1)==A::Save && editor.candidate().target[0]==0);
    editor.saved(false);assert(editor.current()==defaults && !editor.feedback().empty());
    editor.activate(0);assert(editor.targets());
    assert(editor.activate(1)==A::Save);editor.saved(true);assert(editor.current().target[0]==0);
    editor.activate(10);assert(editor.current().target[0]==0); // Reset requires second explicit selection.
    editor.activate(9);assert(editor.page()==1);
    editor.activate(10);assert(editor.current().target[0]==0); // Page change cancelled old reset confirmation.
    assert(editor.activate(10)==A::Save);editor.saved(true);assert(editor.current()==defaults);
    assert(editor.activate(3)==A::None && !editor.targets()); // Physical Minus stays game Back/map.
    editor.activate(9);assert(editor.gyro_page());
    assert(editor.activate(0)==A::Save && !editor.candidate().gyro_assist);
    editor.saved(true);
    assert(editor.activate(0)==A::Save && editor.candidate().gyro_assist);
    editor.saved(true);
    assert(editor.activate(1)==A::Save && editor.candidate().gyro_sensitivity==150);editor.saved(true);
    assert(editor.activate(2)==A::Save && !editor.candidate().gyro_aim_only);editor.saved(true);
    assert(editor.activate(3)==A::Save && editor.candidate().gyro_invert_y);editor.saved(true);
    assert(editor.activate(4)==A::Save && !editor.candidate().gyro_invert_x);editor.saved(true);
    const auto saved_mapping=editor.current();
    assert(editor.activate(5)==A::ToggleNavigation && editor.native_navigation());
    assert(editor.current()==saved_mapping && editor.candidate()==saved_mapping);
    assert(editor.activate(5)==A::ToggleNavigation && !editor.native_navigation());
    for(int row=6;row<8;++row) assert(editor.activate(row)==A::None); // Read-only preset hints.
    editor.activate(9);assert(editor.page()==0);
    editor.activate(0);assert(editor.targets());
    editor.activate(10);assert(editor.page()==1);
    assert(editor.activate(8)==A::None); // Unused option cannot write out-of-range target.
    assert(editor.activate(7)==A::Save && editor.candidate().target[0]==15);editor.saved(true);
    editor.activate(0);editor.activate(9); // Previous target page.
    assert(editor.activate(0)==A::Save && editor.candidate().target[0]==input::kDisabledButton);editor.saved(true);
    editor.activate(11);assert(!editor.is_open());
    editor.open("hk4e",defaults);editor.update({},bounds,false,0,{});
    editor.suspend_input();raw={};raw.a=true;
    assert(editor.update(raw,bounds,false,0,{})==A::None && !editor.targets()); // Source change needs release.
    for(const auto profile:{"hkrpg","endfield"}) {
        editor.open(profile,input::DefaultControllerMapping(profile));editor.activate(8);
        assert(editor.gyro_page() && !editor.gyro_supported());
        for(int i=0;i<8;++i) assert(editor.activate(i)==A::None && !editor.current().gyro_assist);
        assert(!editor.native_navigation());
    }
    editor.open("hk4e",defaults);raw={};editor.update(raw,bounds,false,0,{});
    const overlay::Layout layout(bounds);const auto r=layout.row(0);
    overlay::Point p{r.x+30,r.y+r.h/2};
    editor.update(raw,bounds,true,1,p);editor.update(raw,bounds,true,2,p);
    assert(editor.update(raw,bounds,false,0,{})==A::None && !editor.targets()); // Finger replacement cancels.
    editor.update(raw,bounds,true,1,p);editor.update(raw,bounds,true,1,{0,0});
    assert(editor.update(raw,bounds,false,0,{})==A::None && !editor.targets()); // Drag cancels.
    editor.update(raw,bounds,true,1,p);raw.a=true;
    assert(editor.update(raw,bounds,false,0,{})==A::None && editor.targets()); // One tap, not tap + A.
    raw={};editor.update(raw,bounds,false,0,{});raw.b=true;
    assert(editor.update(raw,bounds,false,0,{})==A::None && !editor.targets());
    raw={};editor.update(raw,bounds,false,0,{});raw.b=true;
    assert(editor.update(raw,bounds,false,0,{})==A::Close);
    const overlay::ControllerDiagramLayout diagram(bounds);
    for(const auto profile:{"hk4e","hkrpg","endfield"}) for(unsigned source=0;source<16;++source) {
        const auto original=input::DefaultControllerMapping(profile);
        editor.open(profile,original);editor.update({},bounds,false,0,{});
        const auto r=diagram.button(source);const overlay::Point point{r.x+r.w/2,r.y+r.h/2};
        assert(editor.update({},bounds,true,1,point)==A::None);
        assert(!editor.targets()); // Never commit on touch down.
        assert(editor.update({},bounds,false,0,{})==A::None && editor.is_open());
        assert(editor.current()==original);
        if(source==input::kLocalMinusButton) {
            assert(!editor.targets() && editor.highlighted_source()==source &&
                editor.feedback()=="Minus is fixed to game Back / map");continue;
        }
        assert(editor.targets() && editor.source()==source && editor.highlighted_source()==source);
        if(editor.page()==1) editor.activate(9);
        assert(editor.activate(1)==A::Save && editor.candidate().target[source]==0);
        editor.saved(true);assert(editor.current().target[source]==0);
        assert(editor.page()==static_cast<int>(source/8) && editor.focus()==static_cast<int>(source%8));
    }
    editor.open("hk4e",defaults);editor.update({},bounds,false,0,{});
    const auto hit=diagram.button(7);const overlay::Point trigger{hit.x+hit.w/2,hit.y+hit.h/2};
    editor.update({},bounds,true,1,trigger);editor.update({},bounds,true,1,{trigger.x+hit.w,trigger.y});
    assert(editor.update({},bounds,false,0,{})==A::None && !editor.targets()); // Drag out cancels.
    editor.update({},bounds,true,1,trigger);editor.update({},bounds,true,2,trigger);
    assert(editor.update({},bounds,false,0,{})==A::None && !editor.targets()); // Finger replacement cancels.
    raw={};raw.zr=true;editor.update(raw,bounds,false,0,{});
    assert(editor.pressed().zr && editor.current()==defaults); // Physical highlight doesn't edit config.
    editor.suspend_input();assert(!editor.pressed().zr);
    editor.update({},bounds,false,0,{});
    // Selecting a diagram source while target selection is open updates that
    // source only, never saves the old pending choice by accident.
    editor.select_source(0);editor.select_source(7);
    assert(editor.targets() && editor.source()==7 && editor.current()==defaults);
    assert(editor.select_source(16)==A::None && editor.source()==7);
    editor.activate(11);assert(!editor.targets() && editor.page()==0);
    editor.update({},bounds,true,1,{diagram.panel.x+5,diagram.panel.y+5});
    assert(editor.update({},bounds,false,0,{})==A::None && editor.is_open()); // Empty schematic is inert.
    editor.open("hk4e",defaults,true);assert(editor.native_navigation());
    assert(editor.displayed()==input::ControllerMapping{} && editor.current()==defaults);
    assert(editor.select_source(0)==A::None && !editor.targets());
    assert(editor.activate(10)==A::None && editor.current()==defaults); // No local reset in native mode.
    editor.activate(8);assert(editor.gyro_page());
    assert(editor.activate(0)==A::None && editor.current()==defaults); // No hidden local gyro mutation.
    assert(editor.activate(5)==A::ToggleNavigation && !editor.native_navigation());
    editor.navigation_saved(false,true);assert(editor.native_navigation()); // Failed save reverts only the UI candidate.
    assert(editor.activate(5)==A::ToggleNavigation && !editor.native_navigation());
    editor.navigation_saved(true,false);assert(!editor.native_navigation() && editor.current()==defaults);
    editor.close();editor.open("hk4e",defaults);assert(!editor.native_navigation());
    editor.open("endfield",input::DefaultControllerMapping("endfield"),true);assert(!editor.native_navigation());
    std::cout<<"PASS controller editor: save failure, restore confirmation, gesture capture, pages, gyro scope\n";
}
