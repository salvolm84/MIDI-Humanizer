// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "PromisedFileDrop.h"

#import <AppKit/AppKit.h>
#include <objc/runtime.h>

#include <vector>

// The drag source in Reaper (WDL's SWELL_InitiateDragDrop) uses the legacy
// dragPromisedFilesOfTypes: API, so the receiving side has to use the matching legacy
// calls. They are deprecated but still work, and they are what Finder answers too.
JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS

namespace mh
{
namespace
{

/** The runtime subclasses we have made, one per JUCE view class. Disposed before JUCE
    disposes its own classes: this static is built after them, so it dies first.
*/
struct PromiseSubclasses
{
    std::vector<std::pair<Class, Class>> baseToSub;

    ~PromiseSubclasses()
    {
        for (auto& [base, sub] : baseToSub)
            objc_disposeClassPair (sub);
    }

    bool isOurs (Class c) const
    {
        for (auto& entry : baseToSub)
            if (entry.second == c)
                return true;

        return false;
    }
};

PromiseSubclasses& subclasses()
{
    static PromiseSubclasses s;
    return s;
}

/** JUCE's view class sitting directly under our subclass. Walks up the hierarchy rather
    than taking the immediate superclass, in case something else (KVO) has subclassed
    the view again since.
*/
Class juceBaseClass (id self)
{
    for (Class c = object_getClass (self); c != nil; c = class_getSuperclass (c))
        if (subclasses().isOurs (c))
            return class_getSuperclass (c);

    jassertfalse;
    return nil;
}

template <typename Fn>
Fn juceImplementation (id self, SEL sel)
{
    return reinterpret_cast<Fn> (class_getMethodImplementation (juceBaseClass (self), sel));
}

juce::ComponentPeer* peerForView (id view)
{
    for (int i = juce::ComponentPeer::getNumPeers(); --i >= 0;)
        if (auto* peer = juce::ComponentPeer::getPeer (i); peer->getNativeHandle() == (void*) view)
            return peer;

    return nullptr;
}

/** True when the drag carries only a file promise. Anything JUCE can read as a real
    file is left to JUCE.
*/
bool isPromiseOnly (id<NSDraggingInfo> sender)
{
    NSPasteboard* pasteboard = [sender draggingPasteboard];

    if (! [[pasteboard types] containsObject: NSFilesPromisePboardType])
        return false;

    NSArray* urls = [pasteboard readObjectsForClasses: @[ [NSURL class] ]
                                              options: @{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    return [urls count] == 0;
}

/** The promised file does not exist until the drop, so while hovering the targets are
    shown stand-in paths that carry the promised extensions. That is all they look at to
    decide whether they want the drag.
*/
juce::StringArray placeholderFiles (id<NSDraggingInfo> sender)
{
    juce::StringArray files;
    id list = [[sender draggingPasteboard] propertyListForType: NSFilesPromisePboardType];

    if ([list isKindOfClass: [NSArray class]])
        for (id type in (NSArray*) list)
            if ([type isKindOfClass: [NSString class]])
                files.add (juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("promised")
                               .withFileExtension (juce::String::fromUTF8 ([(NSString*) type UTF8String]))
                               .getFullPathName());

    return files;
}

juce::ComponentPeer::DragInfo makeDragInfo (juce::ComponentPeer& peer, NSView* view,
                                            id<NSDraggingInfo> sender, juce::StringArray files)
{
    // JUCE's views are flipped, so this is already top-down, like the peer's own space.
    const auto local = [view convertPoint: [sender draggingLocation] fromView: nil];
    const auto screen = peer.localToGlobal (juce::Point<float> ((float) local.x, (float) local.y));

    juce::ComponentPeer::DragInfo info;
    info.files = std::move (files);
    info.position = peer.getComponent().getLocalPoint (nullptr, screen).roundToInt();
    return info;
}

NSDragOperation acceptedOperation (id<NSDraggingInfo> sender)
{
    return ([sender draggingSourceOperationMask] & NSDragOperationCopy) != 0 ? NSDragOperationCopy
                                                                             : NSDragOperationGeneric;
}

/** A fresh folder for each drop, so the sender never finds a name clash and puts up a
    "replace or keep both" alert. Earlier drops are cleared out: the editor reads a
    dropped file straight away and keeps nothing pointing at it.
*/
juce::File makeDropFolder()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("MIDI Humanizer drops");

    for (auto& old : root.findChildFiles (juce::File::findDirectories, false))
        old.deleteRecursively();

    const auto folder = root.getNonexistentChildFile ("drop", {}, false);
    folder.createDirectory();
    return folder;
}

//==============================================================================
NSDragOperation draggingUpdated (id self, SEL sel, id<NSDraggingInfo> sender)
{
    if (isPromiseOnly (sender))
        if (auto* peer = peerForView (self))
            return peer->handleDragMove (makeDragInfo (*peer, self, sender, placeholderFiles (sender)))
                       ? acceptedOperation (sender)
                       : NSDragOperationNone;

    return juceImplementation<NSDragOperation (*) (id, SEL, id)> (self, sel) (self, sel, sender);
}

void draggingExited (id self, SEL sel, id<NSDraggingInfo> sender)
{
    if (isPromiseOnly (sender))
    {
        if (auto* peer = peerForView (self))
            peer->handleDragExit (makeDragInfo (*peer, self, sender, placeholderFiles (sender)));

        return;
    }

    juceImplementation<void (*) (id, SEL, id)> (self, sel) (self, sel, sender);
}

BOOL performDragOperation (id self, SEL sel, id<NSDraggingInfo> sender)
{
    if (! isPromiseOnly (sender))
        return juceImplementation<BOOL (*) (id, SEL, id)> (self, sel) (self, sel, sender);

    auto* peer = peerForView (self);

    if (peer == nullptr)
        return NO;

    // This is the moment the sender writes the file. Reaper renders the item here,
    // synchronously, and returns the names it wrote.
    const auto folder = makeDropFolder();
    auto* path = (NSString* _Nonnull) [NSString stringWithUTF8String: folder.getFullPathName().toRawUTF8()];
    NSURL* destination = [NSURL fileURLWithPath: path isDirectory: YES];
    NSArray* names = [sender namesOfPromisedFilesDroppedAtDestination: destination];

    juce::StringArray files;

    for (id name in names)
        if ([name isKindOfClass: [NSString class]])
            if (const auto file = folder.getChildFile (juce::String::fromUTF8 ([(NSString*) name UTF8String]));
                file.existsAsFile())
                files.add (file.getFullPathName());

    if (files.isEmpty())
    {
        peer->handleDragExit (makeDragInfo (*peer, self, sender, placeholderFiles (sender)));
        return NO;
    }

    return peer->handleDragDrop (makeDragInfo (*peer, self, sender, files)) ? YES : NO;
}

Class makeSubclass (Class base)
{
    const auto name = juce::String (class_getName (base)) + "_MHPromisedDrop";
    Class sub = objc_allocateClassPair (base, name.toRawUTF8(), 0);

    if (sub == nil)
        return nil;

    const auto add = [&] (SEL sel, IMP imp)
    {
        if (auto* method = class_getInstanceMethod (base, sel))
            class_addMethod (sub, sel, imp, method_getTypeEncoding (method));
    };

    // JUCE answers draggingEntered: with its draggingUpdated: handler; so do we.
    add (@selector (draggingEntered:),      (IMP) draggingUpdated);
    add (@selector (draggingUpdated:),      (IMP) draggingUpdated);
    add (@selector (draggingExited:),       (IMP) draggingExited);
    add (@selector (performDragOperation:), (IMP) performDragOperation);

    objc_registerClassPair (sub);
    subclasses().baseToSub.emplace_back (base, sub);
    return sub;
}

Class subclassFor (Class base)
{
    for (auto& [b, sub] : subclasses().baseToSub)
        if (b == base)
            return sub;

    return makeSubclass (base);
}

} // namespace

//==============================================================================
void acceptPromisedFileDrops (juce::Component& component)
{
    auto* peer = component.getPeer();

    if (peer == nullptr)
        return;

    auto* view = (NSView*) peer->getNativeHandle();

    if (view == nil || subclasses().isOurs (object_getClass (view)))
        return;

    Class sub = subclassFor (object_getClass (view));

    if (sub == nil)
        return;

    // Only this one view changes class. JUCE's other windows (menus, tooltips) keep the
    // stock behaviour.
    object_setClass (view, sub);

    NSMutableArray* types = [NSMutableArray arrayWithArray: [view registeredDraggedTypes]];

    if (! [types containsObject: NSFilesPromisePboardType])
        [types addObject: NSFilesPromisePboardType];

    [view registerForDraggedTypes: types];
}

} // namespace mh

JUCE_END_IGNORE_DEPRECATION_WARNINGS
