#include "mihoyo/compact_stream_menu.hpp"
#include <cassert>
#include <set>

int main() {
    using namespace cloudgenshin::overlay;
    CompactStreamMenu menu;
    using Page=CompactStreamMenu::Page;
    assert(menu.count()==6 && menu.page()==Page::Home && !menu.back());
    for(int row:{-1,6,99}) {
        const auto selection=menu.select(row,2,60);
        assert(selection.command==-1 && !selection.navigation && menu.page()==Page::Home);
    }
    std::set<int> commands;
    for(int row:{2,3,5}) {
        const auto selection=menu.select(row,2,60);
        assert(!selection.navigation && selection.command>=0);
        commands.insert(selection.command);
    }
    assert(menu.select(5,2,60).command==10); // Only explicit stop, never a submenu.
    const auto quality=menu.select(0,2,60);
    assert(quality.navigation && quality.command==-1 && quality.focus==2 && menu.count()==4);
    for(int row=0;row<3;++row) {
        const auto selection=menu.select(row,2,60);
        assert(!selection.navigation && selection.command==row);
        commands.insert(selection.command);
    }
    assert(menu.select(3,2,60).focus==0 && menu.page()==Page::Home);
    const auto fps=menu.select(1,2,60);
    assert(fps.navigation && fps.focus==1 && menu.count()==3);
    for(int row=0;row<2;++row) commands.insert(menu.select(row,2,60).command);
    assert(menu.back()==1);
    assert(menu.select(1,2,30).focus==0);
    assert(menu.select(2,2,30).navigation && menu.page()==Page::Home);
    assert(menu.select(4,2,60).navigation && menu.count()==5);
    for(int row=0;row<4;++row) commands.insert(menu.select(row,2,60).command);
    assert(menu.select(4,2,60).focus==4 && menu.page()==Page::Home);
    for(int id=0;id<12;++id) assert(commands.contains(id)); // Nothing removed or renumbered.
    for(const Rect bounds: {Rect{0,0,1280,720},Rect{100,200,1280,720},Rect{0,0,1920,1080}}) {
        for(int page:{-1,0,1,4}) {
            menu.reset();if(page>=0) menu.select(page,2,60);
            const CompactStreamMenuLayout layout(bounds,menu.count());
            assert(layout.row_height>=44 && layout.panel.w<=440 && layout.panel.h<=428);
            assert(bounds.contains({layout.panel.x,layout.panel.y}));
            assert(bounds.contains({layout.panel.x+layout.panel.w-1,layout.panel.y+layout.panel.h-1}));
            assert(layout.hit_row({layout.close.x+2,layout.close.y+2})==-1);
            for(int row=0;row<menu.count();++row) {
                const auto r=layout.row(row);
                assert(layout.hit_row({r.x+5,r.y+r.h/2})==row);
                assert(r.y+r.h<=layout.panel.y+layout.panel.h-40);
                assert(std::string_view(menu.label(row)).size()>0);
            }
        }
    }
    menu.reset();menu.select(4,2,60);assert(menu.page()==Page::More);
    menu.reset();assert(menu.page()==Page::Home && menu.count()==6);
}
