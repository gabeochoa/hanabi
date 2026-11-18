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
 std::printf("test_blitz_emptystate_quote_kbd_facts\n");
 CHECK(el::native_from_tree("std/EmptyState", json{{"p",{{"title","No results"},{"message","Try again"}}}}).text=="No results\nTry again");
 CHECK(el::native_from_tree("std/EmptyState", json{{"p",json::object()}}).text.empty());
 CHECK(el::native_from_tree("std/Quote", json{{"p",{{"text","Ship it"},{"author","Ada"}}}}).text=="\xe2\x80\x9cShip it\xe2\x80\x9d\n\xe2\x80\x94 Ada");
 CHECK(el::native_from_tree("std/Kbd", json{{"p",{{"keys",json::array({"Cmd","K"})}}}}).text=="[Cmd] + [K]");
 auto fc=el::native_from_tree("std/Facts", json{{"p",{{"facts",json::array({json{{"label","Region"},{"value","iad"}}})}}}});
 CHECK(fc.text=="Region: iad");
 CHECK(el::native_from_tree("std/Tokens", json{{"p",{{"tokens",json{{"in",5}}}}}}).text=="in: 5");
 std::printf("test_blitz_spinner_skeleton_avatars_crumbs_switch_person_sources_icon_code\n");
 CHECK(el::native_from_tree("std/Spinner", json{{"p",{{"label","Loading rows"}}}}).text=="\xe2\x97\x8c Loading rows");
 CHECK(el::native_from_tree("std/Skeleton", json{{"p",{{"lines",2}}}}).text.rfind("\xe2\x96\x88",0)==0);
 CHECK(el::native_from_tree("std/AvatarGroup", json{{"p",{{"people",json::array({json{{"name","Ada Lovelace"}},json{{"name","Bob"}}})}}}}).text=="(AL) (B)");
 CHECK(el::native_from_tree("std/Breadcrumbs", json{{"p",{{"items",json::array({json{{"label","Home"}},json{{"label","Queue"}}})}}}}).text=="Home \xe2\x80\xba Queue");
 CHECK(el::native_from_tree("std/Switch", json{{"p",{{"label","Alerts"},{"checked",true}}}}).text=="Alerts [on]");
 CHECK(el::native_from_tree("std/Switch", json{{"p",{{"label","Alerts"},{"checked",false}}}}).text=="Alerts [off]");
 CHECK(el::native_from_tree("std/Person", json{{"p",{{"name","Ada"},{"role","Eng"}}}}).text=="Ada \xe2\x80\x94 Eng");
 CHECK(el::native_from_tree("std/Sources", json{{"p",{{"items",json::array({json{{"title","Doc"},{"url","fburl"}}})}}}}).text=="[1] Doc \xe2\x80\x94 fburl");
 CHECK(el::native_from_tree("std/Icon", json{{"p",{{"name","check"}}}}).text=="\xe2\x97\x86 check");
 CHECK(el::native_from_tree("std/Code", json{{"p",{{"language","py"},{"code","x=1"}}}}).text=="py\nx=1");
 CHECK(el::native_from_tree("std/Breadcrumbs", json{{"p",json::object()}}).text.empty());
 CHECK(el::native_from_tree("std/Unknown", json{{"p",json::object()}}).text.empty());
 if(!failures){std::printf("OK\n");return 0;} return 1;
}
