#import <AppKit/AppKit.h>

#include <string.h>

#include <atomic>

#include <string>
#include <unordered_map>
#include <vector>

#include "a11y_bridge.h"

static std::atomic<unsigned long long> g_pressed{0};

static void hanabi_a11y_queue_press(unsigned long long entity) {
    g_pressed.store(entity, std::memory_order_release);
}

@interface HanabiA11yElement : NSAccessibilityElement
@property(nonatomic, copy) NSString* hanabiName;
@property(nonatomic, copy) NSString* hanabiRole;
@property(nonatomic, assign) BOOL hanabiEnabled;
@property(nonatomic, assign) BOOL hanabiSelected;
@property(nonatomic, assign) BOOL hanabiHasSubmenu;
@property(nonatomic, assign) BOOL hanabiExpanded;
@property(nonatomic, assign) BOOL hanabiPressable;
@property(nonatomic, assign) unsigned long long hanabiEntity;
@end

@implementation HanabiA11yElement

- (void)dealloc {
    [_hanabiName release];
    [_hanabiRole release];
    [super dealloc];
}

- (BOOL)isAccessibilityEnabled {
    return self.hanabiEnabled;
}

- (BOOL)isAccessibilitySelected {
    return self.hanabiSelected;
}

- (BOOL)isAccessibilityExpanded {
    return self.hanabiExpanded;
}

- (BOOL)isAccessibilityElement {
    return YES;
}

- (NSArray<NSString*>*)accessibilityActionNames {
    if (!self.hanabiPressable || !self.hanabiEnabled) return @[];
    return @[ NSAccessibilityPressAction ];
}

- (NSString*)accessibilityActionDescription:(NSString*)action {
    if ([action isEqualToString:NSAccessibilityPressAction]) return @"press";
    return nil;
}

- (void)accessibilityPerformAction:(NSString*)action {
    if (![action isEqualToString:NSAccessibilityPressAction]) return;
    [self accessibilityPerformPress];
}

- (BOOL)accessibilityPerformPress {
    if (!self.hanabiPressable || !self.hanabiEnabled) return NO;
    if (self.hanabiEntity == 0) return NO;
    hanabi_a11y_queue_press(self.hanabiEntity);
    return YES;
}

- (NSString*)accessibilityValueDescription {
    NSMutableString* out = [NSMutableString stringWithString:self.hanabiName];
    if (self.hanabiRole.length > 0)
        [out appendFormat:@", %@", self.hanabiRole];
    if (!self.hanabiEnabled) [out appendString:@", dimmed"];
    if (self.hanabiSelected) [out appendString:@", selected"];
    if (self.hanabiHasSubmenu)
        [out appendString:self.hanabiExpanded ? @", submenu expanded"
                                              : @", submenu"];
    return out;
}

@end

namespace {

NSMutableArray<HanabiA11yElement*>* g_all = nil;
NSMutableArray<HanabiA11yElement*>* g_roots = nil;

NSAccessibilityRole role_for(const char* role) {
    if (role == nullptr) return NSAccessibilityUnknownRole;
    const std::string r(role);
    if (r == "button") return NSAccessibilityButtonRole;
    if (r == "menuitem") return NSAccessibilityMenuItemRole;
    if (r == "menu") return NSAccessibilityMenuRole;
    if (r == "tooltip") return NSAccessibilityStaticTextRole;
    if (r == "tab") return NSAccessibilityRadioButtonRole;
    if (r == "row") return NSAccessibilityRowRole;
    if (r == "field") return NSAccessibilityTextFieldRole;
    if (r == "checkbox") return NSAccessibilityCheckBoxRole;
    if (r == "list") return NSAccessibilityListRole;
    return NSAccessibilityUnknownRole;
}

NSString* action_for(const char* role) {
    if (role == nullptr) return nil;
    const std::string r(role);
    if (r == "button" || r == "menuitem" || r == "tab" || r == "row" ||
        r == "checkbox")
        return NSAccessibilityPressAction;
    return nil;
}

NSView* content_view() {
    if (NSApp == nil) return nil;
    NSWindow* window = [NSApp mainWindow];
    if (window == nil) window = [NSApp keyWindow];
    if (window == nil)
        for (NSWindow* w in [NSApp windows])
            if ([w isVisible]) {
                window = w;
                break;
            }
    if (window == nil) return nil;
    NSView* view = [window contentView];
    if (view == nil) return nil;
    if (![view respondsToSelector:@selector(setAccessibilityChildren:)] ||
        ![view respondsToSelector:@selector(accessibilityChildren)])
        return nil;
    return view;
}

}  // namespace

void native_a11y_publish(const NativeA11yNode* nodes, size_t count) {
    @autoreleasepool {
        if (g_all == nil) g_all = [[NSMutableArray alloc] init];
        if (g_roots == nil) g_roots = [[NSMutableArray alloc] init];
        [g_all removeAllObjects];
        [g_roots removeAllObjects];

        NSView* view = content_view();
        const CGFloat viewH = view != nil ? view.bounds.size.height : 0.0;

        std::unordered_map<std::string, HanabiA11yElement*> byName;
        std::vector<std::string> parents;
        parents.reserve(count);
        unsigned long long synthetic = 1ULL << 62;

        for (size_t i = 0; i < count; ++i) {
            const NativeA11yNode& n = nodes[i];
            if (n.name == nullptr || *n.name == '\0') continue;
            HanabiA11yElement* e =
                [[[HanabiA11yElement alloc] init] autorelease];
            e.hanabiName = [NSString stringWithUTF8String:n.name];
            e.hanabiRole = n.role != nullptr
                               ? [NSString stringWithUTF8String:n.role]
                               : @"";
            e.hanabiEnabled = n.enabled != 0;
            e.hanabiSelected = n.selected != 0;
            e.hanabiHasSubmenu = n.has_submenu != 0;
            e.hanabiExpanded = n.expanded != 0;
            e.hanabiEntity = n.entity != 0 ? n.entity : ++synthetic;
            e.hanabiPressable = action_for(n.role) != nil;

            [e setAccessibilityLabel:e.hanabiName];
            [e setAccessibilityRole:role_for(n.role)];
            [e setAccessibilityFocused:(n.focused != 0)];
            [e setAccessibilityFrameInParentSpace:
                    NSMakeRect(n.x, viewH - n.y - n.height, n.width, n.height)];
            [g_all addObject:e];
            byName[std::string(n.name)] = e;
            parents.emplace_back(n.parent == nullptr ? "" : n.parent);
        }

        std::unordered_map<std::string, NSMutableArray*> kids;
        for (size_t i = 0; i < parents.size(); ++i) {
            HanabiA11yElement* e = g_all[i];
            const std::string& parent = parents[i];
            auto found = parent.empty() ? byName.end() : byName.find(parent);
            if (found == byName.end()) {
                if (view != nil) [e setAccessibilityParent:view];
                [g_roots addObject:e];
                continue;
            }
            [e setAccessibilityParent:found->second];
            NSMutableArray*& list = kids[parent];
            if (list == nil) list = [NSMutableArray array];
            [list addObject:e];
        }
        for (auto& [name, list] : kids) {
            auto found = byName.find(name);
            if (found != byName.end())
                [found->second setAccessibilityChildren:list];
        }

        if (view != nil) [view setAccessibilityChildren:g_roots];
    }
}

size_t native_a11y_published_count(void) {
    return g_all == nil ? 0 : static_cast<size_t>([g_all count]);
}

void native_a11y_describe(const char* name, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (name == nullptr || g_all == nil) return;
    NSString* want = [NSString stringWithUTF8String:name];
    for (HanabiA11yElement* e in g_all) {
        if (![[e accessibilityLabel] isEqualToString:want]) continue;
        const char* utf = [[e accessibilityValueDescription] UTF8String];
        if (utf == nullptr) return;
        strncpy(out, utf, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
}

size_t native_a11y_child_count(const char* name) {
    if (name == nullptr || g_all == nil) return 0;
    NSString* want = [NSString stringWithUTF8String:name];
    for (HanabiA11yElement* e in g_all) {
        if (![[e accessibilityLabel] isEqualToString:want]) continue;
        NSArray* kids = [e accessibilityChildren];
        return kids == nil ? 0 : static_cast<size_t>([kids count]);
    }
    return 0;
}

void native_a11y_parent_of(const char* name, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (name == nullptr || g_all == nil) return;
    NSString* want = [NSString stringWithUTF8String:name];
    for (HanabiA11yElement* e in g_all) {
        if (![[e accessibilityLabel] isEqualToString:want]) continue;
        id parent = [e accessibilityParent];
        if (![parent isKindOfClass:[HanabiA11yElement class]]) return;
        const char* utf = [[parent accessibilityLabel] UTF8String];
        if (utf == nullptr) return;
        strncpy(out, utf, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
}

void native_a11y_role_of(const char* name, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (name == nullptr || g_all == nil) return;
    NSString* want = [NSString stringWithUTF8String:name];
    for (HanabiA11yElement* e in g_all) {
        if (![[e accessibilityLabel] isEqualToString:want]) continue;
        const char* utf = [[e accessibilityRole] UTF8String];
        if (utf == nullptr) return;
        strncpy(out, utf, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
}

unsigned long long native_a11y_take_pressed(void) {
    return g_pressed.exchange(0, std::memory_order_acq_rel);
}

int native_a11y_perform_press(const char* name) {
    if (name == nullptr || g_all == nil) return 0;
    NSString* want = [NSString stringWithUTF8String:name];
    for (HanabiA11yElement* e in g_all) {
        if (![[e accessibilityLabel] isEqualToString:want]) continue;
        if (![[e accessibilityActionNames]
                containsObject:NSAccessibilityPressAction])
            return 0;
        [e accessibilityPerformAction:NSAccessibilityPressAction];
        return 1;
    }
    return 0;
}
