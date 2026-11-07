#include "odh/pie_menu.hpp"

#include <cassert>
#include <algorithm>

int main() {
    using odh::PieMenuChoice;
    const auto menu = odh::make_pie_menu_model({
        PieMenuChoice{4, std::string("Food/Have A Snack")},
        PieMenuChoice{7, std::string("Food/Serve Food")},
        PieMenuChoice{9, std::string("Have A Snack")},
        PieMenuChoice{12, std::string("Food/Have A Snack")},
        PieMenuChoice{15, std::nullopt},
        PieMenuChoice{16, std::string("")},
    });

    assert(menu.size() == 4);
    assert(menu[0].label == "Food" && !menu[0].interaction_row);
    assert(menu[0].children.size() == 3);
    assert(menu[0].children[0].label == "Have A Snack" && menu[0].children[0].interaction_row == 4);
    assert(menu[0].children[1].label == "Serve Food" && menu[0].children[1].interaction_row == 7);
    assert(menu[0].children[2].label == "Have A Snack" && menu[0].children[2].interaction_row == 12);
    assert(menu[1].label == "Have A Snack" && menu[1].interaction_row == 9);
    assert(menu[2].label == "???" && menu[2].interaction_row == 15);
    assert(menu[3].label == "???" && menu[3].interaction_row == 16);
    assert(menu[2].children.empty() && menu[3].children.empty());

    const auto shared_path = odh::make_pie_menu_model({
        PieMenuChoice{2, std::string("Food")},
        PieMenuChoice{3, std::string("Food/Serve")},
    });
    assert(shared_path.size() == 1);
    assert(shared_path[0].label == "Food" && shared_path[0].interaction_row == 2);
    assert(shared_path[0].children.size() == 1);
    assert(shared_path[0].children[0].label == "Serve" && shared_path[0].children[0].interaction_row == 3);

    const auto variants=odh::make_pie_menu_model({
        PieMenuChoice{69,std::string("Social/Talk"),1,0},
        PieMenuChoice{69,std::string("Social/Talk"),1,1},
        PieMenuChoice{96,std::string("Social/Ask"),1},
    });
    assert(variants.size()==1&&variants[0].children.size()==3);
    assert(variants[0].children[0].interaction_row==69&&variants[0].children[0].variant==0);
    assert(variants[0].children[1].interaction_row==69&&variants[0].children[1].variant==1);
    assert(variants[0].children[2].interaction_row==96&&!variants[0].children[2].variant);

    const auto layout = odh::layout_pie_menu(320, 240, {160, 120}, 4, true);
    assert(layout && layout->items.size() == 4 && layout->back_button);
    assert(layout->center[0] == 160 && layout->center[1] == 120);
    assert(layout->items[0].x == layout->center[0] - layout->items[0].width / 2);
    assert(layout->items[0].y == layout->center[1] - 60 - layout->items[0].height);
    assert(layout->items[1].x == layout->center[0] + 60);
    assert(layout->items[2].y == layout->center[1] + 60);
    assert(layout->items[3].x + layout->items[3].width == layout->center[0] - 60);
    for (const auto& item : layout->items) {
        assert(item.x >= 0 && item.y >= 0);
        assert(item.x + item.width <= 320 && item.y + item.height <= 240);
        assert(!item.contains(layout->back_button->x + 1, layout->back_button->y + 1));
    }
    const auto edge_layout = odh::layout_pie_menu(320, 240, {0, 0}, 10, true);
    assert(edge_layout && edge_layout->center[0] == 160 && edge_layout->center[1] == 112);
    for (const auto& item : edge_layout->items) {
        assert(item.x >= 4 && item.y >= 4);
        assert(item.x + item.width <= 316 && item.y + item.height <= 236);
    }
    const auto compact=odh::layout_pie_menu(320,160,{160,80},4,true);
    assert(compact&&compact->items.front().y==4&&compact->items[2].y+24==156);
    for(const unsigned height:{120U,140U,160U,176U,200U,240U,600U}){
        for(const unsigned width:{320U,800U}){
            for(const int x:{0,static_cast<int>(width)/2,static_cast<int>(width)-1})
            for(const int y:{0,static_cast<int>(height)/2,static_cast<int>(height)-1}){
                for(const unsigned count:{1U,2U,4U,5U,8U,9U,10U,17U,53U}){
                    std::vector<bool> reachable(count);
                    std::size_t first=0;
                    do{
                        const auto page=odh::layout_pie_menu(width,height,{x,y},count,true,first);
                        assert(page&&page->page_first==first);
                        std::vector<odh::PieMenuRect> targets{*page->back_button};
                        if(page->previous_page)targets.push_back(*page->previous_page);
                        if(page->next_page)targets.push_back(*page->next_page);
                        for(std::size_t i=0;i<count;++i){
                            const auto& rect=page->items[i];
                            if(rect.width==0){assert(!rect.contains(0,0));continue;}
                            assert(i>=first&&i-first<page->page_size&&!reachable[i]);
                            reachable[i]=true;
                            targets.push_back(rect);
                        }
                        for(std::size_t i=0;i<targets.size();++i){
                            const auto& a=targets[i];
                            assert(a.x>=0&&a.y>=0&&a.x+a.width<=static_cast<int>(width)&&a.y+a.height<=static_cast<int>(height));
                            for(std::size_t j=i+1;j<targets.size();++j){
                                const auto& b=targets[j];
                                assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);
                            }
                        }
                        if(page->previous_page)assert(odh::pie_menu_page_hit(*page,page->previous_page->x,page->previous_page->y)==first-page->page_size);
                        if(!page->next_page)break;
                        const auto next=odh::pie_menu_page_hit(*page,page->next_page->x,page->next_page->y);
                        assert(next&&*next>first);
                        first=*next;
                    }while(first<count);
                    assert(std::all_of(reachable.begin(),reachable.end(),[](bool value){return value;}));
                }
            }
        }
    }
    assert(!odh::layout_pie_menu(96, 80, {48, 40}, 1, false));
}
