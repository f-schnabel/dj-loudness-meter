#include "automation.h"
#include <objbase.h>
#include <UIAutomationClient.h>
#include <wil/com.h>
#include <wil/resource.h>

extern "C" bool automation_widget_right(HWND taskbar, int *right) {
    auto automation = wil::CoCreateInstanceNoThrow<IUIAutomation>(CLSID_CUIAutomation, CLSCTX_INPROC_SERVER);
    wil::com_ptr_nothrow<IUIAutomationElement> root;
    wil::com_ptr_nothrow<IUIAutomationCondition> condition;
    wil::com_ptr_nothrow<IUIAutomationElement> button;
    wil::unique_variant wanted = wil::make_variant_bstr_nothrow(L"WidgetsButton");

    if (!automation || wanted.vt != VT_BSTR ||
        FAILED(automation->ElementFromHandle(taskbar, root.put())) ||
        FAILED(automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, wanted, condition.put())) ||
        FAILED(root->FindFirst(TreeScope_Descendants, condition.get(), button.put())) || !button)
        return false;

    RECT bounds;
    if (FAILED(button->get_CurrentBoundingRectangle(&bounds)) || bounds.right <= bounds.left) return false;
    *right = bounds.right + 2;
    return true;
}
