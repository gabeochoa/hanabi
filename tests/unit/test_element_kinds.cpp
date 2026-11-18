#include <cstdio>
#include <string>
#include "../../src/api/element_kinds.h"
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL: %s (line %d)\n",#c,__LINE__);++failures;}}while(0)
namespace el=api::elements; using nlohmann::json;
int main(){
 std::printf("test_statusdot_carries_tone_and_label\n");
 auto sd=el::native_from_tree("std/StatusDot", json{{"t","std/StatusDot"},{"p",{{"tone","success"},{"label","Serving"}}}});
 CHECK(sd.text=="Serving"); CHECK(sd.tone=="success");
 CHECK(el::native_from_tree("std/StatusDot", json{{"p",{{"label","X"}}}}).text.empty());
 std::printf("test_itemlist_bullets_and_checks\n");
 auto il=el::native_from_tree("std/ItemList", json{{"p",{{"title","Queue"},{"items",json::array({json{{"title","Fix"},{"description","now"}},json{{"title","Done"},{"checked",true}}})}}}});
 CHECK(il.text=="Queue\n\xe2\x80\xa2 Fix \xe2\x80\x94 now\n[x] Done");
 std::printf("test_collapsible_toggle_tabs_grid_timer_chart\n");
 CHECK(el::native_from_tree("std/Collapsible", json{{"p",{{"title","More"},{"summary","2"}}},{"c",json::array({json{{"t","std/Text"},{"p",{{"text","hi"}}}}})}}).text=="\xe2\x96\xb8 More (2)\n  hi");
 json tgTree; tgTree["p"]={{"label","Done"},{"shows","done"}}; tgTree["c"]=json::array({json{{"t","std/Card"},{"p",{{"title","A"},{"tag","done"}}}},json{{"t","std/Card"},{"p",{{"title","B"},{"tag","open"}}}}});
 auto tg=el::native_from_tree("std/Toggle", tgTree);
 CHECK(tg.text=="Done (1)\nA\nB");
 auto tb=el::native_from_tree("std/Tabs", json{{"c",json::array({json{{"p",{{"label","One"}}}},json{{"p",{{"label","Two"}}}}})}});
 CHECK(tb.text.rfind("[One] | Two",0)==0);
 auto gr=el::native_from_tree("std/Grid", json{{"p",{{"columns",2}}},{"c",json::array({json{{"p",{{"title","A"}}}},json{{"p",{{"title","B"}}}},json{{"p",{{"title","C"}}}}})}});
 CHECK(gr.text=="A | B\nC");
 CHECK(el::native_from_tree("std/Timer", json{{"p",{{"label","Deploy"},{"seconds",60}}}}).text=="\xe2\x8f\xb1 Deploy \xe2\x80\x94 60");
 auto ch=el::native_from_tree("std/Chart", json{{"p",{{"title","DAU"},{"kind","line"},{"categories",json::array({"Mon"})},{"series",json::array({json{{"name","A"},{"values",json::array({1})}}})}}}});
 CHECK(ch.text=="DAU (line)\nA: Mon=1");
 CHECK(el::native_from_tree("std/Unknown", json{{"p",json::object()}}).text.empty());
 if(!failures){std::printf("OK\n");return 0;} return 1;
}
