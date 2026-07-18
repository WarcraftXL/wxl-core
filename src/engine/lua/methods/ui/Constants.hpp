// UI constants: integer enum tables (flags, conditions, colors, directions) for wxl.ui.*.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "engine/lua/methods/ui/Common.hpp"

/// Enum constant tables. Field names mirror the ImGui enum suffix (e.g. wxl.ui.WINDOW.NoResize) so modders
/// combine them with LuaJIT's bit library: wxl.ui.begin("W", bit.bor(wxl.ui.WINDOW.NoResize, ...)). Every
/// value is taken from the real ImGui symbol, so a table only ever exposes what this build actually defines.
namespace wxl::lua::methods::ui
{
    /// Sets an integer field on the table currently on top of the stack.
    inline void SetI(lua_State* L, const char* k, lua_Integer v)
    {
        lua_pushinteger(L, v);
        lua_setfield(L, -2, k);
    }

    /// Builds all wxl.ui.* constant tables onto the ui subtable on top of the stack.
    inline void RegisterConstants(lua_State* L)
    {
        // WINDOW: ImGuiWindowFlags_ for begin / begin_child.
        lua_newtable(L);
        SetI(L, "None", ImGuiWindowFlags_None);
        SetI(L, "NoTitleBar", ImGuiWindowFlags_NoTitleBar);
        SetI(L, "NoResize", ImGuiWindowFlags_NoResize);
        SetI(L, "NoMove", ImGuiWindowFlags_NoMove);
        SetI(L, "NoScrollbar", ImGuiWindowFlags_NoScrollbar);
        SetI(L, "NoScrollWithMouse", ImGuiWindowFlags_NoScrollWithMouse);
        SetI(L, "NoCollapse", ImGuiWindowFlags_NoCollapse);
        SetI(L, "AlwaysAutoResize", ImGuiWindowFlags_AlwaysAutoResize);
        SetI(L, "NoBackground", ImGuiWindowFlags_NoBackground);
        SetI(L, "NoSavedSettings", ImGuiWindowFlags_NoSavedSettings);
        SetI(L, "NoMouseInputs", ImGuiWindowFlags_NoMouseInputs);
        SetI(L, "MenuBar", ImGuiWindowFlags_MenuBar);
        SetI(L, "HorizontalScrollbar", ImGuiWindowFlags_HorizontalScrollbar);
        SetI(L, "NoFocusOnAppearing", ImGuiWindowFlags_NoFocusOnAppearing);
        SetI(L, "NoBringToFrontOnFocus", ImGuiWindowFlags_NoBringToFrontOnFocus);
        SetI(L, "AlwaysVerticalScrollbar", ImGuiWindowFlags_AlwaysVerticalScrollbar);
        SetI(L, "AlwaysHorizontalScrollbar", ImGuiWindowFlags_AlwaysHorizontalScrollbar);
        SetI(L, "NoNavInputs", ImGuiWindowFlags_NoNavInputs);
        SetI(L, "NoNavFocus", ImGuiWindowFlags_NoNavFocus);
        SetI(L, "UnsavedDocument", ImGuiWindowFlags_UnsavedDocument);
        SetI(L, "NoNav", ImGuiWindowFlags_NoNav);
        SetI(L, "NoDecoration", ImGuiWindowFlags_NoDecoration);
        SetI(L, "NoInputs", ImGuiWindowFlags_NoInputs);
        lua_setfield(L, -2, "WINDOW");

        // COND: ImGuiCond_ for the set_next_window_* / set_next_item_open helpers. Treat as a plain choice.
        lua_newtable(L);
        SetI(L, "None", ImGuiCond_None);
        SetI(L, "Always", ImGuiCond_Always);
        SetI(L, "Once", ImGuiCond_Once);
        SetI(L, "FirstUseEver", ImGuiCond_FirstUseEver);
        SetI(L, "Appearing", ImGuiCond_Appearing);
        lua_setfield(L, -2, "COND");

        // COL: ImGuiCol_ for push_style_color / get_color.
        lua_newtable(L);
        SetI(L, "Text", ImGuiCol_Text);
        SetI(L, "TextDisabled", ImGuiCol_TextDisabled);
        SetI(L, "WindowBg", ImGuiCol_WindowBg);
        SetI(L, "ChildBg", ImGuiCol_ChildBg);
        SetI(L, "PopupBg", ImGuiCol_PopupBg);
        SetI(L, "Border", ImGuiCol_Border);
        SetI(L, "BorderShadow", ImGuiCol_BorderShadow);
        SetI(L, "FrameBg", ImGuiCol_FrameBg);
        SetI(L, "FrameBgHovered", ImGuiCol_FrameBgHovered);
        SetI(L, "FrameBgActive", ImGuiCol_FrameBgActive);
        SetI(L, "TitleBg", ImGuiCol_TitleBg);
        SetI(L, "TitleBgActive", ImGuiCol_TitleBgActive);
        SetI(L, "TitleBgCollapsed", ImGuiCol_TitleBgCollapsed);
        SetI(L, "MenuBarBg", ImGuiCol_MenuBarBg);
        SetI(L, "ScrollbarBg", ImGuiCol_ScrollbarBg);
        SetI(L, "ScrollbarGrab", ImGuiCol_ScrollbarGrab);
        SetI(L, "ScrollbarGrabHovered", ImGuiCol_ScrollbarGrabHovered);
        SetI(L, "ScrollbarGrabActive", ImGuiCol_ScrollbarGrabActive);
        SetI(L, "CheckMark", ImGuiCol_CheckMark);
        SetI(L, "SliderGrab", ImGuiCol_SliderGrab);
        SetI(L, "SliderGrabActive", ImGuiCol_SliderGrabActive);
        SetI(L, "Button", ImGuiCol_Button);
        SetI(L, "ButtonHovered", ImGuiCol_ButtonHovered);
        SetI(L, "ButtonActive", ImGuiCol_ButtonActive);
        SetI(L, "Header", ImGuiCol_Header);
        SetI(L, "HeaderHovered", ImGuiCol_HeaderHovered);
        SetI(L, "HeaderActive", ImGuiCol_HeaderActive);
        SetI(L, "Separator", ImGuiCol_Separator);
        SetI(L, "SeparatorHovered", ImGuiCol_SeparatorHovered);
        SetI(L, "SeparatorActive", ImGuiCol_SeparatorActive);
        SetI(L, "ResizeGrip", ImGuiCol_ResizeGrip);
        SetI(L, "ResizeGripHovered", ImGuiCol_ResizeGripHovered);
        SetI(L, "ResizeGripActive", ImGuiCol_ResizeGripActive);
        SetI(L, "Tab", ImGuiCol_Tab);
        SetI(L, "TabHovered", ImGuiCol_TabHovered);
        SetI(L, "TabSelected", ImGuiCol_TabSelected);
        SetI(L, "TabDimmed", ImGuiCol_TabDimmed);
        SetI(L, "TabDimmedSelected", ImGuiCol_TabDimmedSelected);
        SetI(L, "PlotLines", ImGuiCol_PlotLines);
        SetI(L, "PlotLinesHovered", ImGuiCol_PlotLinesHovered);
        SetI(L, "PlotHistogram", ImGuiCol_PlotHistogram);
        SetI(L, "PlotHistogramHovered", ImGuiCol_PlotHistogramHovered);
        SetI(L, "TableHeaderBg", ImGuiCol_TableHeaderBg);
        SetI(L, "TableBorderStrong", ImGuiCol_TableBorderStrong);
        SetI(L, "TableBorderLight", ImGuiCol_TableBorderLight);
        SetI(L, "TableRowBg", ImGuiCol_TableRowBg);
        SetI(L, "TableRowBgAlt", ImGuiCol_TableRowBgAlt);
        SetI(L, "TextLink", ImGuiCol_TextLink);
        SetI(L, "TextSelectedBg", ImGuiCol_TextSelectedBg);
        SetI(L, "DragDropTarget", ImGuiCol_DragDropTarget);
        SetI(L, "NavCursor", ImGuiCol_NavCursor);
        SetI(L, "ModalWindowDimBg", ImGuiCol_ModalWindowDimBg);
        lua_setfield(L, -2, "COL");

        // STYLEVAR: ImGuiStyleVar_ for push_style_var. Two-component vars are flagged in their comments.
        lua_newtable(L);
        SetI(L, "Alpha", ImGuiStyleVar_Alpha);
        SetI(L, "DisabledAlpha", ImGuiStyleVar_DisabledAlpha);
        SetI(L, "WindowPadding", ImGuiStyleVar_WindowPadding);           // ImVec2
        SetI(L, "WindowRounding", ImGuiStyleVar_WindowRounding);
        SetI(L, "WindowBorderSize", ImGuiStyleVar_WindowBorderSize);
        SetI(L, "WindowMinSize", ImGuiStyleVar_WindowMinSize);           // ImVec2
        SetI(L, "WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign);     // ImVec2
        SetI(L, "ChildRounding", ImGuiStyleVar_ChildRounding);
        SetI(L, "ChildBorderSize", ImGuiStyleVar_ChildBorderSize);
        SetI(L, "PopupRounding", ImGuiStyleVar_PopupRounding);
        SetI(L, "PopupBorderSize", ImGuiStyleVar_PopupBorderSize);
        SetI(L, "FramePadding", ImGuiStyleVar_FramePadding);             // ImVec2
        SetI(L, "FrameRounding", ImGuiStyleVar_FrameRounding);
        SetI(L, "FrameBorderSize", ImGuiStyleVar_FrameBorderSize);
        SetI(L, "ItemSpacing", ImGuiStyleVar_ItemSpacing);              // ImVec2
        SetI(L, "ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing);    // ImVec2
        SetI(L, "IndentSpacing", ImGuiStyleVar_IndentSpacing);
        SetI(L, "CellPadding", ImGuiStyleVar_CellPadding);              // ImVec2
        SetI(L, "ScrollbarSize", ImGuiStyleVar_ScrollbarSize);
        SetI(L, "ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding);
        SetI(L, "GrabMinSize", ImGuiStyleVar_GrabMinSize);
        SetI(L, "GrabRounding", ImGuiStyleVar_GrabRounding);
        SetI(L, "TabRounding", ImGuiStyleVar_TabRounding);
        SetI(L, "TabBorderSize", ImGuiStyleVar_TabBorderSize);
        SetI(L, "ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign);      // ImVec2
        SetI(L, "SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign); // ImVec2
        SetI(L, "SeparatorTextAlign", ImGuiStyleVar_SeparatorTextAlign);   // ImVec2
        SetI(L, "SeparatorTextPadding", ImGuiStyleVar_SeparatorTextPadding); // ImVec2
        lua_setfield(L, -2, "STYLEVAR");

        // TREE: ImGuiTreeNodeFlags_ for tree_node / collapsing_header.
        lua_newtable(L);
        SetI(L, "None", ImGuiTreeNodeFlags_None);
        SetI(L, "Selected", ImGuiTreeNodeFlags_Selected);
        SetI(L, "Framed", ImGuiTreeNodeFlags_Framed);
        SetI(L, "AllowOverlap", ImGuiTreeNodeFlags_AllowOverlap);
        SetI(L, "NoTreePushOnOpen", ImGuiTreeNodeFlags_NoTreePushOnOpen);
        SetI(L, "NoAutoOpenOnLog", ImGuiTreeNodeFlags_NoAutoOpenOnLog);
        SetI(L, "DefaultOpen", ImGuiTreeNodeFlags_DefaultOpen);
        SetI(L, "OpenOnDoubleClick", ImGuiTreeNodeFlags_OpenOnDoubleClick);
        SetI(L, "OpenOnArrow", ImGuiTreeNodeFlags_OpenOnArrow);
        SetI(L, "Leaf", ImGuiTreeNodeFlags_Leaf);
        SetI(L, "Bullet", ImGuiTreeNodeFlags_Bullet);
        SetI(L, "FramePadding", ImGuiTreeNodeFlags_FramePadding);
        SetI(L, "SpanAvailWidth", ImGuiTreeNodeFlags_SpanAvailWidth);
        SetI(L, "SpanFullWidth", ImGuiTreeNodeFlags_SpanFullWidth);
        SetI(L, "SpanAllColumns", ImGuiTreeNodeFlags_SpanAllColumns);
        SetI(L, "CollapsingHeader", ImGuiTreeNodeFlags_CollapsingHeader);
        lua_setfield(L, -2, "TREE");

        // TAB: ImGuiTabBarFlags_ (tab bar) merged with ImGuiTabItemFlags_ (tab item), by suffix.
        lua_newtable(L);
        SetI(L, "None", ImGuiTabBarFlags_None);
        SetI(L, "Reorderable", ImGuiTabBarFlags_Reorderable);
        SetI(L, "AutoSelectNewTabs", ImGuiTabBarFlags_AutoSelectNewTabs);
        SetI(L, "TabListPopupButton", ImGuiTabBarFlags_TabListPopupButton);
        SetI(L, "NoCloseWithMiddleMouseButton", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton);
        SetI(L, "NoTabListScrollingButtons", ImGuiTabBarFlags_NoTabListScrollingButtons);
        SetI(L, "NoTooltip", ImGuiTabBarFlags_NoTooltip);
        SetI(L, "FittingPolicyShrink", ImGuiTabBarFlags_FittingPolicyShrink);
        SetI(L, "FittingPolicyScroll", ImGuiTabBarFlags_FittingPolicyScroll);
        SetI(L, "ItemUnsavedDocument", ImGuiTabItemFlags_UnsavedDocument);
        SetI(L, "ItemSetSelected", ImGuiTabItemFlags_SetSelected);
        SetI(L, "ItemNoReorder", ImGuiTabItemFlags_NoReorder);
        SetI(L, "ItemLeading", ImGuiTabItemFlags_Leading);
        SetI(L, "ItemTrailing", ImGuiTabItemFlags_Trailing);
        lua_setfield(L, -2, "TAB");

        // TABLE: ImGuiTableFlags_ for begin_table.
        lua_newtable(L);
        SetI(L, "None", ImGuiTableFlags_None);
        SetI(L, "Resizable", ImGuiTableFlags_Resizable);
        SetI(L, "Reorderable", ImGuiTableFlags_Reorderable);
        SetI(L, "Hideable", ImGuiTableFlags_Hideable);
        SetI(L, "Sortable", ImGuiTableFlags_Sortable);
        SetI(L, "NoSavedSettings", ImGuiTableFlags_NoSavedSettings);
        SetI(L, "ContextMenuInBody", ImGuiTableFlags_ContextMenuInBody);
        SetI(L, "RowBg", ImGuiTableFlags_RowBg);
        SetI(L, "BordersInnerH", ImGuiTableFlags_BordersInnerH);
        SetI(L, "BordersOuterH", ImGuiTableFlags_BordersOuterH);
        SetI(L, "BordersInnerV", ImGuiTableFlags_BordersInnerV);
        SetI(L, "BordersOuterV", ImGuiTableFlags_BordersOuterV);
        SetI(L, "BordersH", ImGuiTableFlags_BordersH);
        SetI(L, "BordersV", ImGuiTableFlags_BordersV);
        SetI(L, "BordersInner", ImGuiTableFlags_BordersInner);
        SetI(L, "BordersOuter", ImGuiTableFlags_BordersOuter);
        SetI(L, "Borders", ImGuiTableFlags_Borders);
        SetI(L, "NoBordersInBody", ImGuiTableFlags_NoBordersInBody);
        SetI(L, "SizingFixedFit", ImGuiTableFlags_SizingFixedFit);
        SetI(L, "SizingFixedSame", ImGuiTableFlags_SizingFixedSame);
        SetI(L, "SizingStretchProp", ImGuiTableFlags_SizingStretchProp);
        SetI(L, "SizingStretchSame", ImGuiTableFlags_SizingStretchSame);
        SetI(L, "NoHostExtendX", ImGuiTableFlags_NoHostExtendX);
        SetI(L, "NoHostExtendY", ImGuiTableFlags_NoHostExtendY);
        SetI(L, "PreciseWidths", ImGuiTableFlags_PreciseWidths);
        SetI(L, "NoClip", ImGuiTableFlags_NoClip);
        SetI(L, "PadOuterX", ImGuiTableFlags_PadOuterX);
        SetI(L, "NoPadOuterX", ImGuiTableFlags_NoPadOuterX);
        SetI(L, "NoPadInnerX", ImGuiTableFlags_NoPadInnerX);
        SetI(L, "ScrollX", ImGuiTableFlags_ScrollX);
        SetI(L, "ScrollY", ImGuiTableFlags_ScrollY);
        SetI(L, "SortMulti", ImGuiTableFlags_SortMulti);
        SetI(L, "SortTristate", ImGuiTableFlags_SortTristate);
        lua_setfield(L, -2, "TABLE");

        // TABLE_COLUMN: ImGuiTableColumnFlags_ for table_setup_column.
        lua_newtable(L);
        SetI(L, "None", ImGuiTableColumnFlags_None);
        SetI(L, "Disabled", ImGuiTableColumnFlags_Disabled);
        SetI(L, "DefaultHide", ImGuiTableColumnFlags_DefaultHide);
        SetI(L, "DefaultSort", ImGuiTableColumnFlags_DefaultSort);
        SetI(L, "WidthStretch", ImGuiTableColumnFlags_WidthStretch);
        SetI(L, "WidthFixed", ImGuiTableColumnFlags_WidthFixed);
        SetI(L, "NoResize", ImGuiTableColumnFlags_NoResize);
        SetI(L, "NoReorder", ImGuiTableColumnFlags_NoReorder);
        SetI(L, "NoHide", ImGuiTableColumnFlags_NoHide);
        SetI(L, "NoClip", ImGuiTableColumnFlags_NoClip);
        SetI(L, "NoSort", ImGuiTableColumnFlags_NoSort);
        SetI(L, "PreferSortAscending", ImGuiTableColumnFlags_PreferSortAscending);
        SetI(L, "PreferSortDescending", ImGuiTableColumnFlags_PreferSortDescending);
        SetI(L, "IndentEnable", ImGuiTableColumnFlags_IndentEnable);
        SetI(L, "IndentDisable", ImGuiTableColumnFlags_IndentDisable);
        SetI(L, "WidthMask", ImGuiTableColumnFlags_WidthMask_);
        lua_setfield(L, -2, "TABLE_COLUMN");

        // TABLE_ROW: ImGuiTableRowFlags_ for table_next_row.
        lua_newtable(L);
        SetI(L, "None", ImGuiTableRowFlags_None);
        SetI(L, "Headers", ImGuiTableRowFlags_Headers);
        lua_setfield(L, -2, "TABLE_ROW");

        // SELECTABLE: ImGuiSelectableFlags_ for selectable.
        lua_newtable(L);
        SetI(L, "None", ImGuiSelectableFlags_None);
        SetI(L, "NoAutoClosePopups", ImGuiSelectableFlags_NoAutoClosePopups);
        SetI(L, "SpanAllColumns", ImGuiSelectableFlags_SpanAllColumns);
        SetI(L, "AllowDoubleClick", ImGuiSelectableFlags_AllowDoubleClick);
        SetI(L, "Disabled", ImGuiSelectableFlags_Disabled);
        SetI(L, "AllowOverlap", ImGuiSelectableFlags_AllowOverlap);
        SetI(L, "Highlight", ImGuiSelectableFlags_Highlight);
        lua_setfield(L, -2, "SELECTABLE");

        // INPUT: ImGuiInputTextFlags_ for input_text / input_* widgets.
        lua_newtable(L);
        SetI(L, "None", ImGuiInputTextFlags_None);
        SetI(L, "CharsDecimal", ImGuiInputTextFlags_CharsDecimal);
        SetI(L, "CharsHexadecimal", ImGuiInputTextFlags_CharsHexadecimal);
        SetI(L, "CharsScientific", ImGuiInputTextFlags_CharsScientific);
        SetI(L, "CharsUppercase", ImGuiInputTextFlags_CharsUppercase);
        SetI(L, "CharsNoBlank", ImGuiInputTextFlags_CharsNoBlank);
        SetI(L, "AllowTabInput", ImGuiInputTextFlags_AllowTabInput);
        SetI(L, "EnterReturnsTrue", ImGuiInputTextFlags_EnterReturnsTrue);
        SetI(L, "EscapeClearsAll", ImGuiInputTextFlags_EscapeClearsAll);
        SetI(L, "CtrlEnterForNewLine", ImGuiInputTextFlags_CtrlEnterForNewLine);
        SetI(L, "ReadOnly", ImGuiInputTextFlags_ReadOnly);
        SetI(L, "Password", ImGuiInputTextFlags_Password);
        SetI(L, "AlwaysOverwrite", ImGuiInputTextFlags_AlwaysOverwrite);
        SetI(L, "AutoSelectAll", ImGuiInputTextFlags_AutoSelectAll);
        SetI(L, "NoHorizontalScroll", ImGuiInputTextFlags_NoHorizontalScroll);
        SetI(L, "NoUndoRedo", ImGuiInputTextFlags_NoUndoRedo);
        lua_setfield(L, -2, "INPUT");

        // HOVERED: ImGuiHoveredFlags_ for is_item_hovered / is_window_hovered.
        lua_newtable(L);
        SetI(L, "None", ImGuiHoveredFlags_None);
        SetI(L, "ChildWindows", ImGuiHoveredFlags_ChildWindows);
        SetI(L, "RootWindow", ImGuiHoveredFlags_RootWindow);
        SetI(L, "AnyWindow", ImGuiHoveredFlags_AnyWindow);
        SetI(L, "NoPopupHierarchy", ImGuiHoveredFlags_NoPopupHierarchy);
        SetI(L, "AllowWhenBlockedByPopup", ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        SetI(L, "AllowWhenBlockedByActiveItem", ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        SetI(L, "AllowWhenDisabled", ImGuiHoveredFlags_AllowWhenDisabled);
        SetI(L, "RectOnly", ImGuiHoveredFlags_RectOnly);
        SetI(L, "ForTooltip", ImGuiHoveredFlags_ForTooltip);
        SetI(L, "DelayShort", ImGuiHoveredFlags_DelayShort);
        SetI(L, "DelayNormal", ImGuiHoveredFlags_DelayNormal);
        lua_setfield(L, -2, "HOVERED");

        // FOCUSED: ImGuiFocusedFlags_ for is_window_focused.
        lua_newtable(L);
        SetI(L, "None", ImGuiFocusedFlags_None);
        SetI(L, "ChildWindows", ImGuiFocusedFlags_ChildWindows);
        SetI(L, "RootWindow", ImGuiFocusedFlags_RootWindow);
        SetI(L, "AnyWindow", ImGuiFocusedFlags_AnyWindow);
        SetI(L, "RootAndChildWindows", ImGuiFocusedFlags_RootAndChildWindows);
        lua_setfield(L, -2, "FOCUSED");

        // DIR: ImGuiDir for arrow_button.
        lua_newtable(L);
        SetI(L, "None", ImGuiDir_None);
        SetI(L, "Left", ImGuiDir_Left);
        SetI(L, "Right", ImGuiDir_Right);
        SetI(L, "Up", ImGuiDir_Up);
        SetI(L, "Down", ImGuiDir_Down);
        lua_setfield(L, -2, "DIR");

        // MOUSE: button ids for the is_mouse_* / *_clicked queries.
        lua_newtable(L);
        SetI(L, "left", ImGuiMouseButton_Left);
        SetI(L, "right", ImGuiMouseButton_Right);
        SetI(L, "middle", ImGuiMouseButton_Middle);
        lua_setfield(L, -2, "MOUSE");

        // COMBO: ImGuiComboFlags_ for begin_combo.
        lua_newtable(L);
        SetI(L, "None", ImGuiComboFlags_None);
        SetI(L, "PopupAlignLeft", ImGuiComboFlags_PopupAlignLeft);
        SetI(L, "HeightSmall", ImGuiComboFlags_HeightSmall);
        SetI(L, "HeightRegular", ImGuiComboFlags_HeightRegular);
        SetI(L, "HeightLarge", ImGuiComboFlags_HeightLarge);
        SetI(L, "HeightLargest", ImGuiComboFlags_HeightLargest);
        SetI(L, "NoArrowButton", ImGuiComboFlags_NoArrowButton);
        SetI(L, "NoPreview", ImGuiComboFlags_NoPreview);
        SetI(L, "WidthFitPreview", ImGuiComboFlags_WidthFitPreview);
        lua_setfield(L, -2, "COMBO");

        // SLIDER: ImGuiSliderFlags_ for the drag / slider widgets.
        lua_newtable(L);
        SetI(L, "None", ImGuiSliderFlags_None);
        SetI(L, "Logarithmic", ImGuiSliderFlags_Logarithmic);
        SetI(L, "NoRoundToFormat", ImGuiSliderFlags_NoRoundToFormat);
        SetI(L, "NoInput", ImGuiSliderFlags_NoInput);
        SetI(L, "WrapAround", ImGuiSliderFlags_WrapAround);
        SetI(L, "ClampOnInput", ImGuiSliderFlags_ClampOnInput);
        SetI(L, "AlwaysClamp", ImGuiSliderFlags_AlwaysClamp);
        lua_setfield(L, -2, "SLIDER");

        // COLOR_EDIT: ImGuiColorEditFlags_ for color_edit3 / color_edit4 / color_button.
        lua_newtable(L);
        SetI(L, "None", ImGuiColorEditFlags_None);
        SetI(L, "NoAlpha", ImGuiColorEditFlags_NoAlpha);
        SetI(L, "NoPicker", ImGuiColorEditFlags_NoPicker);
        SetI(L, "NoOptions", ImGuiColorEditFlags_NoOptions);
        SetI(L, "NoSmallPreview", ImGuiColorEditFlags_NoSmallPreview);
        SetI(L, "NoInputs", ImGuiColorEditFlags_NoInputs);
        SetI(L, "NoTooltip", ImGuiColorEditFlags_NoTooltip);
        SetI(L, "NoLabel", ImGuiColorEditFlags_NoLabel);
        SetI(L, "NoDragDrop", ImGuiColorEditFlags_NoDragDrop);
        SetI(L, "AlphaBar", ImGuiColorEditFlags_AlphaBar);
        SetI(L, "DisplayRGB", ImGuiColorEditFlags_DisplayRGB);
        SetI(L, "DisplayHSV", ImGuiColorEditFlags_DisplayHSV);
        SetI(L, "DisplayHex", ImGuiColorEditFlags_DisplayHex);
        SetI(L, "Uint8", ImGuiColorEditFlags_Uint8);
        SetI(L, "Float", ImGuiColorEditFlags_Float);
        SetI(L, "PickerHueBar", ImGuiColorEditFlags_PickerHueBar);
        SetI(L, "PickerHueWheel", ImGuiColorEditFlags_PickerHueWheel);
        lua_setfield(L, -2, "COLOR_EDIT");

        // POPUP: ImGuiPopupFlags_ for begin_popup_context_item.
        lua_newtable(L);
        SetI(L, "None", ImGuiPopupFlags_None);
        SetI(L, "MouseButtonLeft", ImGuiPopupFlags_MouseButtonLeft);
        SetI(L, "MouseButtonRight", ImGuiPopupFlags_MouseButtonRight);
        SetI(L, "MouseButtonMiddle", ImGuiPopupFlags_MouseButtonMiddle);
        SetI(L, "NoReopen", ImGuiPopupFlags_NoReopen);
        SetI(L, "NoOpenOverExistingPopup", ImGuiPopupFlags_NoOpenOverExistingPopup);
        SetI(L, "NoOpenOverItems", ImGuiPopupFlags_NoOpenOverItems);
        lua_setfield(L, -2, "POPUP");
    }
}
