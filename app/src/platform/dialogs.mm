// Objective-C++ glue for native macOS alerts. Compiled WITHOUT ARC (-fno-objc-arc)
// like eye.mm and the imgui backends, so we [release] manually.
#import <AppKit/AppKit.h>
#include "platform/dialogs.h"

namespace sw {

UnsavedChoice askUnsaved() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"有未儲存的變更";
  alert.informativeText = @"目前的專案有尚未儲存的變更，要先儲存嗎？";
  [alert addButtonWithTitle:@"儲存"];     // NSAlertFirstButtonReturn
  [alert addButtonWithTitle:@"不儲存"];   // NSAlertSecondButtonReturn
  [alert addButtonWithTitle:@"取消"];     // NSAlertThirdButtonReturn
  NSModalResponse r = [alert runModal];
  [alert release];
  if (r == NSAlertFirstButtonReturn) return UnsavedChoice::Save;
  if (r == NSAlertSecondButtonReturn) return UnsavedChoice::DontSave;
  return UnsavedChoice::Cancel;
}

void showError(const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"無法完成操作";
  alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
  [alert addButtonWithTitle:@"好"];
  [alert runModal];
  [alert release];
}

}  // namespace sw
