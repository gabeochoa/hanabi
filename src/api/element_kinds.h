#pragma once
// Native text for the std element kinds the reference drew natively
// (eec0bb96 cluster), parsed from the wire tree the way element_table.h
// parses std/Table. Pure: no UI, so tests/unit/test_element_kinds.cpp pins it.
// 'the reference' drew StatusDot/Toggle/Collapsible/ItemList/Tabs/Grid/Timer/
// Chart/Artifact as cards; Hanabi's generic element row drew only the server
// projection. This gives each kind its own lines; the row draws those lines
// instead of the projection, and StatusDot also carries a tone for a drawn dot.
// Unknown / malformed kinds return "" so the projection fallback stands.
#include <optional>
#include <string>
#include <vector>
#include "../../vendor/nlohmann/json.hpp"
#include "element_chart.h"
namespace api::elements {
inline std::string kind_str(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return v.dump();
    return {};
}
inline std::string kind_prop(const nlohmann::json& p, const char* a, const char* b = nullptr) {
    if (p.is_object()) {
        if (p.contains(a)) { auto s = kind_str(p[a]); if (!s.empty()) return s; }
        if (b && p.contains(b)) return kind_str(p[b]);
    }
    return {};
}
inline std::vector<std::string> kind_tags(const nlohmann::json& v) {
    std::vector<std::string> out;
    if (v.is_string()) {
        std::string cur; for (char c: v.get<std::string>()) { if (c==' '||c==','||c=='\t') { if(!cur.empty()){out.push_back(cur);cur.clear();} } else cur+=c; } if(!cur.empty()) out.push_back(cur);
    } else if (v.is_array()) for (auto& e: v) { auto s=kind_str(e); if(!s.empty()) out.push_back(s); }
    return out;
}
// One-line summary of a child tree node, from its own props.
inline std::string kind_child_text(const nlohmann::json& n) {
    if (!n.is_object()) return {};
    const auto& p = n.contains("p") ? n["p"] : nlohmann::json::object();
    std::string t = n.contains("t") ? kind_str(n["t"]) : "";
    for (const char* k : {"title","text","label","name","caption","value"}) { auto s=kind_prop(p,k); if(!s.empty()) return s; }
    if (p.is_object() && p.contains("items") && p["items"].is_array()) return t + ": " + std::to_string(p["items"].size()) + " items";
    // nested children: use first child's text
    if (n.contains("c") && n["c"].is_array() && !n["c"].empty()) return kind_child_text(n["c"][0]);
    return t;
}
inline std::vector<std::string> kind_children_text(const nlohmann::json& tree) {
    std::vector<std::string> out;
    if (tree.is_object() && tree.contains("c") && tree["c"].is_array())
        for (auto& c: tree["c"]) { auto s=kind_child_text(c); if(!s.empty()) out.push_back(s); if(out.size()>=50) break; }
    return out;
}
struct NativeKind { std::string text; std::string tone; std::string chart_source; };
inline NativeKind native_from_tree(const std::string& el, const nlohmann::json& tree) {
    NativeKind r; if (!tree.is_object()) return r;
    const auto& p = tree.contains("p") ? tree["p"] : nlohmann::json::object();
    auto kids = kind_children_text(tree);
    if (el=="std/StatusDot") {
        auto label=kind_prop(p,"label"); auto tone=kind_prop(p,"tone");
        if(label.empty()||tone.empty()) return r;
        r.text=label; r.tone=tone; return r;
    }
    if (el=="std/ItemList") {
        if(!p.is_object()||!p.contains("items")||!p["items"].is_array()) return r;
        std::string out=kind_prop(p,"title"); if(!out.empty()) out+="\n";
        int n=0; for(auto& it: p["items"]) { if(n++>=50){out+="\xe2\x80\xa6\n";break;}
            std::string title=kind_prop(it,"title","label"); if(title.empty()) title=kind_child_text(it);
            std::string desc=kind_prop(it,"description","subtitle");
            std::string meta=kind_prop(it,"meta"); bool checked=it.is_object()&&it.contains("checked")&&it["checked"].is_boolean();
            out += checked ? (it["checked"].get<bool>()?"[x] ":"[ ] ") : "\xe2\x80\xa2 ";
            out+=title; if(!desc.empty()) out+=" \xe2\x80\x94 "+desc; if(!meta.empty()) out+=" ("+meta+")"; out+="\n"; }
        while(!out.empty()&&out.back()=='\n') out.pop_back(); r.text=out; return r;
    }
    if (el=="std/Collapsible") {
        auto title=kind_prop(p,"title","label"); if(title.empty()) return r;
        std::string summary=kind_prop(p,"summary");
        r.text="\xe2\x96\xb8 "+title+(summary.empty()?"":" ("+summary+")");
        for(auto& k: kids) r.text+="\n  "+k; return r;
    }
    if (el=="std/Toggle") {
        auto label=kind_prop(p,"label"); auto shows=kind_prop(p,"shows"); if(label.empty()) return r;
        int cnt=0; if(tree.contains("c")&&tree["c"].is_array()) for(auto& c: tree["c"]) { auto tags=kind_tags(c.contains("p")&&c["p"].is_object()?c["p"].value("tag",nlohmann::json()):nlohmann::json()); for(auto& t:tags) if(t==shows) cnt++; }
        bool counts = !(p.is_object()&&p.contains("counts")&&p["counts"].is_boolean()&&!p["counts"].get<bool>());
        r.text=label+(counts&&!shows.empty()?" ("+std::to_string(cnt)+")":"");
        for(auto& k: kids) r.text+="\n"+k; return r;
    }
    if (el=="std/Tabs") {
        // children are PageTab-like nodes with p.label/id, or p.tabs array
        std::vector<std::string> labels;
        if(tree.contains("c")&&tree["c"].is_array()) for(auto& c: tree["c"]) { auto l=kind_prop(c.contains("p")?c["p"]:nlohmann::json::object(),"label","title"); if(!l.empty()) labels.push_back(l); }
        if(labels.empty()&&p.is_object()&&p.contains("tabs")&&p["tabs"].is_array()) for(auto& t: p["tabs"]) { auto l=kind_prop(t,"label","title"); if(!l.empty()) labels.push_back(l); }
        if(labels.empty()) return r;
        r.text="["+labels[0]+"]"; for(size_t i=1;i<labels.size();++i) r.text+=" | "+labels[i];
        for(auto& k: kids) r.text+="\n"+k; return r;
    }
    if (el=="std/Grid") {
        if(kids.empty()) return r; int cols=2; if(p.is_object()&&p.contains("columns")&&p["columns"].is_number_integer()) cols=(int)p["columns"].get<long long>(); if(cols<1) cols=1; if(cols>6) cols=6;
        for(size_t i=0;i<kids.size();i+=cols){ if(i) r.text+="\n"; for(int j=0;j<cols&&i+j<kids.size();++j){ if(j) r.text+=" | "; r.text+=kids[i+j]; } } return r;
    }
    if (el=="std/Timer") {
        auto label=kind_prop(p,"label","title"); auto val=kind_prop(p,"endsAt","deadline"); if(val.empty()) val=kind_prop(p,"seconds","duration");
        if(label.empty()&&val.empty()) return r; r.text="\xe2\x8f\xb1 "+label+(val.empty()?"":" \xe2\x80\x94 "+val); return r;
    }
    if (el=="std/Chart") {
        // Data as lines (values readable) AND a plot: the converter
        // (element_chart.h) reads inline series, rows+encoding and
        // segments; chart_source is the fence JSON the transcript's
        // fence plotter draws. Kinds it cannot mark keep the lines only.
        auto chart=element_chart_from_props(p); if(!chart) return r;
        r.text=(chart->title.empty()?"Chart":chart->title)+(chart->kind.empty()?"":" ("+chart->kind+")");
        for(auto& s: chart->series) { r.text+="\n"+s.name+":"; size_t i=0; for(auto& pt: s.points){ std::string v="-"; if(pt.second) v=(*pt.second==std::trunc(*pt.second)&&std::fabs(*pt.second)<9e15)?std::to_string((long long)*pt.second):nlohmann::json(*pt.second).dump(); r.text+=" "+pt.first+"="+v; if(++i>=200) break; } }
        r.chart_source=element_chart_source(*chart);
        return r;
    }
    if (el=="std/Query") {
        // Live rows (std.data / GraphQL, b7d2981d): the host that runs the
        // query stamps one child per row into this tree (or re-emits the
        // children as their own elements); Hanabi has no client query
        // channel to run graphql/data itself -- see the bank README for
        // the seam. What draws here is the query's own description plus
        // any stamped children the tree already carries.
        std::string src=kind_prop(p,"declared");
        if(src.empty()&&p.is_object()&&p.contains("data")&&p["data"].is_object()) {
            auto eng=kind_prop(p["data"],"engine"); src=eng;
            auto ds=kind_prop(p["data"].contains("scuba")?p["data"]["scuba"]:nlohmann::json::object(),"dataset");
            auto ent=kind_prop(p["data"].contains("ods")?p["data"]["ods"]:nlohmann::json::object(),"entity");
            if(!ds.empty()) src+=": "+ds; if(!ent.empty()) src+=": "+ent;
        }
        if(src.empty()&&p.is_object()&&p.contains("graphql")) src="GraphQL";
        if(src.empty()) return r;
        r.text="Live query \xe2\x80\x94 "+src;
        auto each=kind_prop(p,"each"); auto key=kind_prop(p,"key"); auto refresh=kind_prop(p,"refreshMs");
        if(!each.empty()) r.text+="\neach "+each+(key.empty()?"":" \xc2\xb7 key "+key)+(refresh.empty()?"":" \xc2\xb7 refresh "+refresh+"ms");
        for(auto& k: kids) r.text+="\n"+k;
        return r;
    }
    // W45 blitz (the reference, 4b34c545 cluster): smallest kinds first.
    // All are pure text; kinds needing new layout engines (Calendar, Tree,
    // Diff, NestApp, Carousel, Pagination, ScrollArea, TabbedPage, PageTab,
    // Dialog, SelectableCards, ClickableCard, Toolbar, Menu, Popover, Delta)
    // are documented divergences in the bank README, not built here.
    if (el=="std/EmptyState") {
        auto title=kind_prop(p,"title","heading"); auto msg=kind_prop(p,"message","description"); if(title.empty()) title=msg, msg="";
        if(title.empty()&&!kids.empty()) title=kids[0];
        if(title.empty()) return r; r.text=title; if(!msg.empty()) r.text+="\n"+msg;
        for(auto& k: kids) if(k!=title) r.text+="\n"+k; return r;
    }
    if (el=="std/Quote") {
        auto text=kind_prop(p,"text","quote"); auto by=kind_prop(p,"author","source"); auto cite=kind_prop(p,"citation");
        if(text.empty()&&!kids.empty()) text=kids[0]; if(text.empty()) return r;
        r.text="\xe2\x80\x9c"+text+"\xe2\x80\x9d"; if(!by.empty()) r.text+="\n\xe2\x80\x94 "+by+(cite.empty()?"":" ("+cite+")"); return r;
    }
    if (el=="std/Kbd") {
        std::vector<std::string> keys;
        if(p.is_object()&&p.contains("keys")) keys=kind_tags(p["keys"]);
        if(keys.empty()&&p.is_object()&&p.contains("shortcuts")&&p["shortcuts"].is_array()) for(auto& s:p["shortcuts"]) { auto k=kind_prop(s,"keys","key"); if(!k.empty()) keys.push_back(k); }
        auto single=kind_prop(p,"key","shortcut"); if(single.empty()) single=kind_prop(p,"label"); if(keys.empty()&&!single.empty()) keys.push_back(single);
        if(keys.empty()) return r; r.text="["+keys[0]+"]"; for(size_t i=1;i<keys.size();++i) r.text+=" + ["+keys[i]+"]"; return r;
    }
    if (el=="std/Facts"||el=="std/Tokens") {
        std::string out=kind_prop(p,"title"); bool any=false;
        auto add=[&](const std::string& l,const std::string& v){ if(l.empty()||v.empty()) return; if(!out.empty()) out+="\n"; out+=l+": "+v; any=true; };
        const char* arrKey = el=="std/Facts"?"facts":"tokens";
        if(p.is_object()&&p.contains(arrKey)&&p[arrKey].is_array()) for(auto& f:p[arrKey]) add(kind_prop(f,"label","name"),kind_prop(f,"value","text"));
        else if(p.is_object()&&p.contains(arrKey)&&p[arrKey].is_object()) for(auto& [k,v]:p[arrKey].items()) add(k,kind_str(v));
        else if(p.is_object()&&p.contains("items")&&p["items"].is_array()) for(auto& f:p["items"]) add(kind_prop(f,"label","name"),kind_prop(f,"value","text"));
        if(!any) return r; r.text=out; return r;
    }
    if (el=="std/Spinner") { auto l=kind_prop(p,"label","text"); r.text="\xe2\x97\x8c "+(l.empty()?"Loading\xe2\x80\xa6":l); return r; }
    if (el=="std/Skeleton") {
        int lines=3; if(p.is_object()&&p.contains("lines")&&p["lines"].is_number_integer()) lines=(int)p["lines"].get<long long>(); if(p.is_object()&&p.contains("rows")&&p["rows"].is_number_integer()) lines=(int)p["rows"].get<long long>(); if(lines<1)lines=1; if(lines>12)lines=12;
        for(int i=0;i<lines;++i){ if(i) r.text+="\n"; r.text+= i%3==2?"\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88":"\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88"; } return r;
    }
    if (el=="std/AvatarGroup") {
        std::vector<std::string> names;
        const char* keysArr[3]={"people","members","items"};
        if(p.is_object()) for(auto* kk:keysArr) if(p.contains(kk)&&p[kk].is_array()) for(auto& a:p[kk]) { auto n=kind_prop(a,"name","label"); if(n.empty()) n=kind_str(a); if(!n.empty()) names.push_back(n); }
        if(names.empty()) names=kids; if(names.empty()) return r;
        int maxShow=5; if(p.is_object()&&p.contains("max")&&p["max"].is_number_integer()) maxShow=(int)p["max"].get<long long>(); if(maxShow<1)maxShow=1;
        for(size_t i=0;i<names.size()&&i<(size_t)maxShow;++i){ if(i) r.text+=" "; std::string ini; { std::string w; for(char c:names[i]){ if(c==' '){ if(!w.empty()) ini+=w[0]; w.clear(); } else w+=c; } if(!w.empty()) ini+=w[0]; if(ini.size()>2) ini.resize(2); for(auto& ch:ini) if(ch>='a'&&ch<='z') ch-=32; } r.text+="("+ini+")"; }
        if(names.size()>(size_t)maxShow) r.text+=" +"+std::to_string(names.size()-maxShow); return r;
    }
    if (el=="std/Breadcrumbs") {
        std::vector<std::string> crumbs;
        if(p.is_object()&&p.contains("items")&&p["items"].is_array()) for(auto& b:p["items"]) { auto l=kind_prop(b,"label","title"); if(l.empty()) l=kind_str(b); if(!l.empty()) crumbs.push_back(l); }
        if(crumbs.empty()) crumbs=kids; if(crumbs.empty()) return r;
        r.text=crumbs[0]; for(size_t i=1;i<crumbs.size();++i) r.text+=" \xe2\x80\xba "+crumbs[i]; return r;
    }
    if (el=="std/Switch") {
        auto label=kind_prop(p,"label","title"); if(label.empty()) return r;
        bool on=false; bool has=false; if(p.is_object()) for(const char* k:{"checked","on","enabled","value"}) if(p.contains(k)&&p[k].is_boolean()){ on=p[k].get<bool>(); has=true; break; }
        r.text=label+(has?(on?" [on]":" [off]"):""); auto d=kind_prop(p,"description"); if(!d.empty()) r.text+=" \xe2\x80\x94 "+d; return r;
    }
    if (el=="std/Person") {
        auto name=kind_prop(p,"name","label"); if(name.empty()&&!kids.empty()) name=kids[0]; if(name.empty()) return r;
        auto sub=kind_prop(p,"subtitle","role"); auto mail=kind_prop(p,"email","meta"); r.text=name; if(!sub.empty()) r.text+=" \xe2\x80\x94 "+sub; if(!mail.empty()) r.text+=" ("+mail+")"; return r;
    }
    if (el=="std/Sources") {
        if(!p.is_object()||!p.contains("items")||!p["items"].is_array()) { if(kids.empty()) return r; r.text=kids[0]; for(size_t i=1;i<kids.size();++i) r.text+="\n"+kids[i]; return r; }
        int n=0; for(auto& s:p["items"]){ if(n>=20) break; auto t=kind_prop(s,"title","label"); auto u=kind_prop(s,"url","domain"); if(t.empty()) t=u; if(t.empty()) continue; if(n) r.text+="\n"; r.text+="["+std::to_string(++n)+"] "+t; if(!u.empty()&&u!=t) r.text+=" \xe2\x80\x94 "+u; } if(n==0) return r; return r;
    }
    if (el=="std/Icon") { auto n=kind_prop(p,"name","icon"); if(n.empty()) return r; auto l=kind_prop(p,"label"); r.text="\xe2\x97\x86 "+n+(l.empty()?"":" \xe2\x80\x94 "+l); return r; }
    if (el=="std/Code") {
        auto code=kind_prop(p,"code","text"); auto lang=kind_prop(p,"language","lang"); if(code.empty()) return r; r.text=(lang.empty()?"":lang+"\n")+code; return r;
    }
    if (el=="std/Artifact") {
        auto name=kind_prop(p,"title","name"); auto file=kind_prop(p,"file","filename"); if(name.empty()&&file.empty()) return r; r.text=name+(file.empty()?"":" \xe2\x80\x94 "+file); return r;
    }
    return r;
}
inline std::string native_text_from_tree(const std::string& el, const nlohmann::json& tree){ return native_from_tree(el,tree).text; }
} // namespace api::elements
