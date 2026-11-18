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
struct NativeKind { std::string text; std::string tone; };
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
        // Data as lines (values readable); plot drawing is the fence chart's job - see README divergence.
        auto title=kind_prop(p,"title"); auto kind=kind_prop(p,"kind","type"); if(!p.is_object()||!p.contains("series")||!p["series"].is_array()) return r;
        r.text=(title.empty()?"Chart":title)+(kind.empty()?"":" ("+kind+")");
        std::vector<std::string> cats; if(p.contains("categories")&&p["categories"].is_array()) for(auto& c:p["categories"]) cats.push_back(kind_str(c));
        for(auto& s: p["series"]) { std::string name=kind_prop(s,"name"); r.text+="\n"+(name.empty()?"Series":name)+":"; if(s.is_object()&&s.contains("values")&&s["values"].is_array()) { size_t i=0; for(auto& v:s["values"]){ r.text+=" "; if(i<cats.size()) r.text+=cats[i]+"="; r.text+=v.is_null()?"-":kind_str(v); if(++i>=200) break; } } }
        return r;
    }
    if (el=="std/Artifact") {
        auto name=kind_prop(p,"title","name"); auto file=kind_prop(p,"file","filename"); if(name.empty()&&file.empty()) return r; r.text=name+(file.empty()?"":" \xe2\x80\x94 "+file); return r;
    }
    return r;
}
inline std::string native_text_from_tree(const std::string& el, const nlohmann::json& tree){ return native_from_tree(el,tree).text; }
} // namespace api::elements
