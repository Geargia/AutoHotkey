/*
AutoHotkey

Copyright 2003 Chris Mallett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "stdafx.h" // pre-compiled headers
#include "script.h"
#include "globaldata.h" // for a lot of things
#include "application.h" // for MsgSleep()
#include "window.h" // for SetForegroundWindowEx()


ResultType Script::PerformMenu(char *aMenu, char *aCommand, char *aParam3, char *aParam4)
{
	if (mMenuUseErrorLevel)
		g_ErrorLevel->Assign(ERRORLEVEL_NONE);  // Set default, which is "none" for the Menu command.

	#define RETURN_MENU_ERROR(msg, info) return mMenuUseErrorLevel ? g_ErrorLevel->Assign(ERRORLEVEL_ERROR) \
		: ScriptError(msg ERR_ABORT, info)
	#define RETURN_IF_NOT_TRAY if (!is_tray) RETURN_MENU_ERROR(ERR_MENUTRAY, aMenu)

	MenuCommands menu_command = Line::ConvertMenuCommand(aCommand);
	if (menu_command == MENU_CMD_INVALID)
		RETURN_MENU_ERROR(ERR_MENUCOMMAND2, aCommand);

	bool is_tray = !stricmp(aMenu, "tray");

	// Handle early on anything that doesn't require the menu to be found or created:
	switch(menu_command)
	{
	case MENU_CMD_USEERRORLEVEL:
		mMenuUseErrorLevel = (Line::ConvertOnOff(aParam3) != TOGGLED_OFF);
		// Even though the state may have changed by the above, it doesn't seem necessary
		// to adjust on the fly for the purpose of this particular return.  In other words,
		// the old mode will be in effect for this one return:
		return OK;

	case MENU_CMD_TIP:
		RETURN_IF_NOT_TRAY;
		if (*aParam3)
		{
			if (!mTrayIconTip)
				mTrayIconTip = SimpleHeap::Malloc(sizeof(mNIC.szTip)); // SimpleHeap improves avg. case mem load.
			if (mTrayIconTip)
				strlcpy(mTrayIconTip, aParam3, sizeof(mNIC.szTip));
		}
		else // Restore tip to default.
			if (mTrayIconTip)
				*mTrayIconTip = '\0';
		if (mNIC.hWnd) // i.e. only update the tip if the tray icon exists (can't work otherwise).
		{
			UPDATE_TIP_FIELD
			Shell_NotifyIcon(NIM_MODIFY, &mNIC);  // Currently not checking its result.
		}
		return OK;

	case MENU_CMD_ICON:
	{
		RETURN_IF_NOT_TRAY;
		if (!*aParam3)
		{
			g_NoTrayIcon = false;
			if (!mNIC.hWnd) // The icon doesn't exist, so create it.
			{
				CreateTrayIcon();
				UpdateTrayIcon(true);  // Force the icon into the correct pause/suspend state.
			}
			return OK;
		}

		// Otherwise, user has specified a custom icon:
		if (*aParam3 == '*' && !*(aParam3 + 1)) // Restore the standard icon.
		{
			if (mCustomIcon)
			{
				DestroyIcon(mCustomIcon);
				mCustomIcon = NULL;  // To indicate that there is no custom icon.
				if (mCustomIconFile)
					*mCustomIconFile = '\0';
				mCustomIconNumber = 0;
				UpdateTrayIcon(true);  // Need to use true in this case too.
			}
			return OK;
		}

		int icon_number = *aParam4 ? ATOI(aParam4) : 1;  // Need int to support signed values.
		if (icon_number < 1)
			icon_number = 1;

		// Alternate method, untested and probably doesn't work on EXEs:
		// Specifying size 32x32 probably saves memory if the icon file is large:
		//new_icon = LoadImage(NULL, aParam3, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
		HICON new_icon = ExtractIcon(g_hInstance, aParam3, icon_number - 1);
		if (!new_icon)
			RETURN_MENU_ERROR("Icon could not be loaded.", aParam3);
		if (mCustomIcon) // Destroy the old one first to free its resources.
			DestroyIcon(mCustomIcon);

		mCustomIcon = new_icon;
		mCustomIconNumber = icon_number;
		if (!mCustomIconFile)
			mCustomIconFile = SimpleHeap::Malloc(MAX_PATH); // SimpleHeap improves avg. case mem load.
		if (mCustomIconFile)
		{
			// Get the full path in case it's a relative path.  This is documented and it's done in case
			// the script ever changes its working directory:
			char full_path[MAX_PATH], *filename_marker;
			if (GetFullPathName(aParam3, sizeof(full_path) - 1, full_path, &filename_marker))
				strlcpy(mCustomIconFile, full_path, MAX_PATH);
			else
				strlcpy(mCustomIconFile, aParam3, MAX_PATH);
		}

		if (!g_NoTrayIcon)
			UpdateTrayIcon(true);  // Need to use true in this case too.
		return OK;
	}

	case MENU_CMD_NOICON:
		RETURN_IF_NOT_TRAY;
		g_NoTrayIcon = true;
		if (mNIC.hWnd) // Since it exists, destroy it.
		{
			Shell_NotifyIcon(NIM_DELETE, &mNIC); // Remove it.
			mNIC.hWnd = NULL;  // Set this as an indicator that tray icon is not installed.
			// but don't do DestroyMenu() on mTrayMenu->mMenu (if non-NULL) since it may have been
			// changed by the user to have the custom items on top of the standard items,
			// for example, and we don't want to lose that ordering in case the script turns
			// the icon back on at some future time during this session.
		}
		return OK;

	case MENU_CMD_MAINWINDOW:
		RETURN_IF_NOT_TRAY;
#ifdef AUTOHOTKEYSC
		if (!g_AllowMainWindow)
		{
			g_AllowMainWindow = true;
			// Rather than using InsertMenu() to insert the item in the right position,
			// which makes the code rather unmaintainable, it seems best just to recreate
			// the entire menu.  This will result in the standard menu items going back
			// up to the top of the menu if the user previously had them at the bottom,
			// but it seems too rare to worry about, especially since it's easy to
			// work around that:
			if (mTrayMenu->mIncludeStandardItems)
				mTrayMenu->Destroy(); // It will be recreated automatically the next time the user displays it.
			// else there's no need.
		}
#endif
        return OK;

	case MENU_CMD_NOMAINWINDOW:
		RETURN_IF_NOT_TRAY;
#ifdef AUTOHOTKEYSC
		if (g_AllowMainWindow)
		{
			g_AllowMainWindow = false;
			// See comments in the prior case above for why it's done this way vs. using DeleteMenu():
			if (mTrayMenu->mIncludeStandardItems)
				mTrayMenu->Destroy(); // It will be recreated automatically the next time the user displays it.
			// else there's no need.
		}
#endif
		return OK;
	} // switch()


	// Now that most opportunities to return an error have passed, find or create the menu, since
	// all the commands that haven't already been fully handled above will need it:
	UserMenu *menu = FindMenu(aMenu);
	if (!menu)
	{
		if (menu_command != MENU_CMD_ADD) // Menus can be created only in conjuction with the ADD command.
			RETURN_MENU_ERROR(ERR_MENU, aMenu);
		if (   !(menu = AddMenu(aMenu))   )
			RETURN_MENU_ERROR("Out of mem", aMenu);
	}

	// The above has found or added the menu for use below.

	switch(menu_command)
	{
	case MENU_CMD_SHOW:
		return menu->Display();
	case MENU_CMD_ADD:
		if (*aParam3) // Since a menu item name was given, it's not a separator line.
			break;    // Let a later switch() handle it.
		if (!menu->AddItem("", 0, NULL, NULL))
			RETURN_MENU_ERROR("Can't add separator.", "");
		return OK;
	case MENU_CMD_DELETE:
		if (*aParam3) // Since a menu item name was given, an item is being deleted, not the whole menu.
			break;    // Let a later switch() handle it.
		if (menu == mTrayMenu)
			RETURN_MENU_ERROR("Tray menu must not be deleted.", "");
		return ScriptDeleteMenu(menu);
	case MENU_CMD_DELETEALL:
		return menu->DeleteAllItems();
	case MENU_CMD_DEFAULT:
		if (*aParam3) // Since a menu item has been specified, let a later switch() handle it.
			break;
		//else no menu item, so it's the same as NoDefault: fall through to the next case.
	case MENU_CMD_NODEFAULT:
		return menu->SetDefault();
	case MENU_CMD_STANDARD:
		return menu->IncludeStandardItems();
	case MENU_CMD_NOSTANDARD:
		return menu->ExcludeStandardItems();
	}

	// All the remaining commands need a menu item to operate upon, or some other requirement met below.

	char *new_name = "";
	if (menu_command == MENU_CMD_RENAME) // aParam4 contains the menu item's new name in this case.
	{
		new_name = aParam4;
		aParam4 = "";
	}

	// The above has handled all cases that don't require a menu item to be found or added,
	// including the adding separator lines.  So at the point, it is necessary to either find
	// or create a menu item.  The latter only occurs for the ADD command.
	if (!*aParam3)
		RETURN_MENU_ERROR("Parameter #3 must not be blank in this case.", "");

	// Seems best to avoid performance enhancers such as (Label *)mAttribute here, since the "Menu"
	// command has so many modes of operation that would be difficult to parse at load-time:
	Label *target_label = NULL;  // Set default.
	UserMenu *submenu = NULL;    // Set default.
	if (menu_command == MENU_CMD_ADD) // Labels and submenus are only used in conjuction with the ADD command.
	{
		if (!*aParam4) // Allow the label/submenu to default to the menu name.
			aParam4 = aParam3; // Note that aParam3 will be blank in the case of a separator line.
		if (*aParam4)
		{
			if (*aParam4 == ':') // It's a submenu.
			{
				++aParam4;
				if (   !(submenu = FindMenu(aParam4))   )
					RETURN_MENU_ERROR(ERR_SUBMENU, aParam4);
				// Before going further: since a submenu has been specified, make sure that the parent
				// menu is not included anywhere in the nested hierarchy of that submenu's submenus.
				// The OS doesn't seem to like that, creating empty or strange menus if it's attempted:
				if (   submenu && (submenu == menu || submenu->ContainsMenu(menu))   )
					RETURN_MENU_ERROR("This submenu must not contain its parent menu.", aParam4);
			}
			else // It's a label.
				if (   !(target_label = FindLabel(aParam4))   )
					RETURN_MENU_ERROR(ERR_MENULABEL, aParam4);
		}
	}

	// Find the menu item name AND its previous item (needed for the DELETE command) in the linked list:
	UserMenuItem *mi, *menu_item = NULL, *menu_item_prev = NULL; // Set defaults.
	for (menu_item = menu->mFirstMenuItem
		; menu_item
		; menu_item_prev = menu_item, menu_item = menu_item->mNextMenuItem)
		if (!stricmp(menu_item->mName, aParam3)) // Match found (case insensitive).
			break;
	if (!menu_item)  // menu item doesn't exist, so create it (but only if the command is ADD).
	{
		if (menu_command != MENU_CMD_ADD)
			// Seems best not to create menu items on-demand like this because they might get put into
			// an incorrect position (i.e. it seems better than menu changes be kept separate from
			// menu additions):
			RETURN_MENU_ERROR("The specified menu item cannot be changed because it doesn't exist.", aParam3);

		// Otherwise: Adding a new item that doesn't yet exist.
		// Need to find a menuID that isn't already in use by one of the other menu items.
		// Can't use (ID_TRAY_USER + mMenuItemCount) because a menu item in the middle of
		// the list may have been deleted, in which case that value would already be in
		// use by the last item.  But we can accelerate the peformance of the below
		// by starting at an ID we expect to be available most of the time, and continue
		// looking from there.  UPDATE: It seems best not to accelerate this way because
		// if during a script instance's lifetime a lot of deletes and adds are done, it's
		// possible that we could run out of IDs.  So start check at the very first one
		// to ensure that none are permanently "wasted":
		UINT candidate_id = ID_TRAY_USER;
		bool id_in_use;
		UserMenu *m;
		for (candidate_id = ID_TRAY_USER; ; ++candidate_id) // FOR EACH ID
		{
			id_in_use = false;  // Reset the default each iteration (overridden if the below finds a match).
			for (m = mFirstMenu; m; m = m->mNextMenu) // FOR EACH MENU
			{
				for (mi = m->mFirstMenuItem; mi; mi = mi->mNextMenuItem) // FOR EACH MENU ITEM
				{
					if (mi->mMenuID == candidate_id)
					{
						id_in_use = true;
						break;
					}
				}
				if (id_in_use) // No point in searching the other menus, since it's now known to be in use.
					break;
			}
			if (!id_in_use) // Break before the loop increments candidate_id.
				break;
		}
		if (!menu->AddItem(aParam3, candidate_id, target_label, submenu))
			RETURN_MENU_ERROR("Can't add menu item.", aParam3);
		return OK;  // Item has been successfully added with the correct properties.
	} // if (!menu_item)

	// Above has found the correct menu_item to operate upon (it already returned if
	// the item was just created).  Since the item was found, the UserMenu's popup
	// menu must already exist because a UserMenu object can't have menu items unless
	// its menu exists.

	switch (menu_command)
	{
	case MENU_CMD_ADD:
		// This is only reached if the ADD command is being used to update the label or submenu of an
		// existing menu item (since it would have returned above if the item was just newly created).
		return menu->ModifyItem(menu_item, target_label, submenu);
	case MENU_CMD_RENAME:
		if (!menu->RenameItem(menu_item, new_name))
			RETURN_MENU_ERROR("The menu item's new name must not match that of an existing item.", new_name);
		return OK;
	case MENU_CMD_CHECK:
		return menu->CheckItem(menu_item);
	case MENU_CMD_UNCHECK:
		return menu->UncheckItem(menu_item);
	case MENU_CMD_TOGGLECHECK:
		return menu->ToggleCheckItem(menu_item);
	case MENU_CMD_ENABLE:
		return menu->EnableItem(menu_item);
	case MENU_CMD_DISABLE: // Disables and grays the item.
		return menu->DisableItem(menu_item);
	case MENU_CMD_TOGGLEENABLE:
		return menu->ToggleEnableItem(menu_item);
	case MENU_CMD_DEFAULT:
		return menu->SetDefault(menu_item);
	case MENU_CMD_DELETE:
		return menu->DeleteItem(menu_item, menu_item_prev);
	} // switch()
	return FAIL;  // Should never be reached, but avoids compiler warning and improves bug detection.
}



UserMenu *Script::FindMenu(char *aMenuName)
// Returns the UserMenu whose name matches aMenuName, or NULL if not found.
{
	if (!aMenuName || !*aMenuName) return NULL;
	for (UserMenu *menu = mFirstMenu; menu != NULL; menu = menu->mNextMenu)
		if (!stricmp(menu->mName, aMenuName)) // Match found.
			return menu;
	return NULL; // No match found.
}



UserMenu *Script::AddMenu(char *aMenuName)
// Caller must have already ensured aMenuName doesn't exist yet in the list.
// Returns the newly created UserMenu object.
{
	if (!aMenuName || !*aMenuName) return NULL;
	UserMenu *menu = new UserMenu(aMenuName);
	if (!menu)
		return NULL;  // Caller should show error if desired.
	if (!mFirstMenu)
		mFirstMenu = mLastMenu = menu;
	else
	{
		mLastMenu->mNextMenu = menu;
		// This must be done after the above:
		mLastMenu = menu;
	}
	++mMenuCount;  // Only after memory has been successfully allocated.
	return menu;
}



ResultType Script::ScriptDeleteMenu(UserMenu *aMenu)
// Deletes a UserMenu object and all the UserMenuItem objects that belong to it.
// Any UserMenuItem object that has a submenu attached to it does not result in
// that submenu being deleted, even if no other menus are using that submenu
// (i.e. the user must delete all menus individually -- this avoids problems with
// recursion if menus are submenus of each other, which seems justified given
// how rarely a menu really *needs* to be deleted by a script.  Any menus
// which have aMenu as one of their submenus will have that menu item deleted
// from their menus to avoid any chance of problems due to non-existent or NULL
// submenus.
{
	// Delete any other menu's menu item that has aMenu as its attached submenu:
	UserMenuItem *mi, *mi_prev, *mi_to_delete;
	for (UserMenu *m = mFirstMenu; m; m = m->mNextMenu)
		if (m != aMenu) // Don't bother with this menu even if it's submenu of itself, since it will be destroyed anyway.
			for (mi = m->mFirstMenuItem, mi_prev = NULL; mi;)
			{
				mi_to_delete = mi;
				mi = mi->mNextMenuItem;
				if (mi_to_delete->mSubmenu == aMenu)
					m->DeleteItem(mi_to_delete, mi_prev);
				else
					mi_prev = mi_to_delete;
			}
	// Remove aMenu from the linked list.  First find the item that occurs prior the aMenu in the list:
	UserMenu *aMenu_prev;
	for (aMenu_prev = mFirstMenu; aMenu_prev; aMenu_prev = aMenu_prev->mNextMenu)
		if (aMenu_prev->mNextMenu == aMenu)
			break;
	if (aMenu == mLastMenu)
		mLastMenu = aMenu_prev; // Can be NULL if the list will now be empty.
	if (aMenu_prev) // there is another item prior to aMenu in the linked list.
		aMenu_prev->mNextMenu = aMenu->mNextMenu; // Can be NULL if aMenu was the last one.
	else // aMenu was the first one in the list.
		mFirstMenu = aMenu->mNextMenu; // Can be NULL if the list will now be empty.
	// Do this last when its contents are no longer needed.  Its destructor will delete all
	// the items in the menu and destroy the OS menu itself:
	delete aMenu;
	--mMenuCount;
	return OK; // For caller convenience.
}



// Macros for use with the below methods:
#define aMenuItem_ID (aMenuItem->mSubmenu ? GetSubmenuPos(aMenuItem->mSubmenu->mMenu) : aMenuItem->mMenuID)
#define aMenuItem_MF_BY (aMenuItem->mSubmenu ? MF_BYPOSITION : MF_BYCOMMAND)

#ifdef AUTOHOTKEYSC
#define CHANGE_DEFAULT_IF_NEEDED \
	if (mDefault == aMenuItem)\
	{\
		if (mMenu)\
		{\
			if (this == g_script.mTrayMenu)\
				SetMenuDefaultItem(mMenu, mIncludeStandardItems && g_AllowMainWindow ? ID_TRAY_OPEN : -1, FALSE);\
			else\
				SetMenuDefaultItem(mMenu, -1, FALSE);\
		}\
		mDefault = NULL;\
	}
#else
#define CHANGE_DEFAULT_IF_NEEDED \
	if (mDefault == aMenuItem)\
	{\
		if (mMenu)\
		{\
			if (this == g_script.mTrayMenu)\
				SetMenuDefaultItem(mMenu, mIncludeStandardItems ? ID_TRAY_OPEN : -1, FALSE);\
			else\
				SetMenuDefaultItem(mMenu, -1, FALSE);\
		}\
		mDefault = NULL;\
	}
#endif



ResultType UserMenu::AddItem(char *aName, UINT aMenuID, Label *aLabel, UserMenu *aSubmenu)
// Caller must have already ensured that aName does not yet exist as a user-defined menu item
// in this->mMenu.
{
	UserMenuItem *menu_item = new UserMenuItem(aName, aMenuID, aLabel, aSubmenu, this);
	if (!menu_item) // Should also be very rare.
		return FAIL;
	if (!mFirstMenuItem)
		mFirstMenuItem = mLastMenuItem = menu_item;
	else
	{
		mLastMenuItem->mNextMenuItem = menu_item;
		// This must be done after the above:
		mLastMenuItem = menu_item;
	}
	++mMenuItemCount;  // Only after memory has been successfully allocated.
	return OK;
}



ResultType UserMenu::DeleteItem(UserMenuItem *aMenuItem, UserMenuItem *aMenuItemPrev)
{
	// Remove this menu item from the linked list:
	if (aMenuItem == mLastMenuItem)
		mLastMenuItem = aMenuItemPrev; // Can be NULL if the list will now be empty.
	if (aMenuItemPrev) // there is another item prior to aMenuItem in the linked list.
		aMenuItemPrev->mNextMenuItem = aMenuItem->mNextMenuItem; // Can be NULL if aMenuItem was the last one.
	else // aMenuItem was the first one in the list.
		mFirstMenuItem = aMenuItem->mNextMenuItem; // Can be NULL if the list will now be empty.
	CHANGE_DEFAULT_IF_NEEDED  // Should do this before freeing aMenuItem's memory.
	if (mMenu) // Delete the item from the menu.
		DeleteMenu(mMenu, aMenuItem_ID, aMenuItem_MF_BY);
	delete aMenuItem; // Do this last when its contents are no longer needed.
	--mMenuItemCount;
	return OK;
}



ResultType UserMenu::DeleteAllItems()
{
	if (!mFirstMenuItem)
		return OK;  // If there are no user-defined menu items, it's already in the correct state.
	// Remove all menu items from the linked list and from the menu.  First destroy the menu since
	// it's probably better to start off fresh than have the destructor individually remove each
	// menu item as the items in the linked list are deleted.  In addition, this avoids the need
	// to find any submenus by position:
	Destroy(); // if mStandardMenuItems is true, the menu will be recreated later when needed.
	// The destructor relies on the fact that the above destroys the menu but does not recreate it.
	// This is because popup menus, not being affiliated with window, must be destroyed with
	// DestroyMenu() to ensure a clean exit (resources freed).
	UserMenuItem *menu_item_to_delete;
	for (UserMenuItem *mi = mFirstMenuItem; mi;)
	{
		menu_item_to_delete = mi;
		mi = mi->mNextMenuItem;
		delete menu_item_to_delete;
	}
	mFirstMenuItem = mLastMenuItem = NULL;
	mMenuItemCount = 0;
	mDefault = NULL;  // i.e. there can't be a *user-defined* default item anymore, even if this is the tray.
	return OK;
}



ResultType UserMenu::ModifyItem(UserMenuItem *aMenuItem, Label *aLabel, UserMenu *aSubmenu)
// Modify the label or submenu of a menu item (exactly one of these should be NULL and the other not).
// If a menu item becomes a submenu, we don't relinquish its ID in case it's ever made a normal item
// again (avoids the need to re-lookup a unique ID).
{
	aMenuItem->mLabel = aLabel;  // This will be NULL if this menu item is a separator or submenu.
	if (aMenuItem->mSubmenu == aSubmenu) // Below relies on this check.
		return OK;
	if (!mMenu)
	{
		aMenuItem->mSubmenu = aSubmenu;  // Just set the indicator for when the menu is later created.
		return OK;
	}

	// Otherwise, since the OS menu exists, one of these is to be done to aMenuItem in it:
	// 1) Change a submenu to point to a different menu.
	// 2) Change a submenu so that it becomes a normal menu item.
	// 3) Change a normal menu item into a submenu.

	// Since Create() ensures that aSubmenu is non-null whenever this->mMenu is non-null, this is just
	// an extra safety check in case some other method destroyed aSubmenu since then:
	if (aSubmenu)
		if (!aSubmenu->Create()) // Create if needed.  No error msg since so rare.
			return FAIL;

	MENUITEMINFO mii;
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_SUBMENU | MIIM_ID;
	mii.hSubMenu = aSubmenu ? aSubmenu->mMenu : NULL;
	// If this submenu is being back into a normal menu item, the ID must be re-specified
	// because an item that was formerly submenu will not have a real ID due to OS behavior:
	mii.wID = aMenuItem->mMenuID;
	if (SetMenuItemInfo(mMenu, aMenuItem_ID, aMenuItem->mSubmenu != NULL, &mii))
	{
		// Submenu was just made into a different submenu or converted into a normal menu item.
		// Since the OS (as an undocumented side effect) sometimes destroys the menu itself when
		// a submenu is changed in this way, update our state to indicate that the menu handle
		// is no longer valid:
		if (aMenuItem->mSubmenu && aMenuItem->mSubmenu->mMenu && !IsMenu(aMenuItem->mSubmenu->mMenu))
		{
			UserMenu *temp = aMenuItem->mSubmenu;
			aMenuItem->mSubmenu = aSubmenu; // Should be done before the below so that Destroy() sees the change.
			temp->Destroy();
		}
		else
			aMenuItem->mSubmenu = aSubmenu;
	}
	// else no error msg and return OK so that the thread will continue.  This may help catch
	// bugs in the course of normal use of this feature.
	return OK;
}



ResultType UserMenu::RenameItem(UserMenuItem *aMenuItem, char *aNewName)
// Caller should specify "" for aNewName to convert aMenuItem into a separator.
// Returns FAIL if the new name conflicts with an existing name.
{
	#define RENAME_MENU_UPDATE \
		if (*aNewName)\
			strlcpy(aMenuItem->mName, aNewName, sizeof(aMenuItem->mName));\
		else\
		{\
			*aMenuItem->mName = '\0';\
			aMenuItem->mMenuID = 0;\
		} // Last line above: Free up an ID since separators currently can't be converted back into items.

	if (!mMenu) // Just update the member variables for later use when the menu is created.
	{
		RENAME_MENU_UPDATE
		return OK;
	}

	MENUITEMINFO mii;
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_TYPE;
	mii.dwTypeData = aNewName;

	if (*aNewName)
	{
		// Names must be unique only within each menu:
		for (UserMenuItem *mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
			if (!stricmp(mi->mName, aNewName)) // Match found (case insensitive).
				return FAIL; // Caller should display an error message.
		mii.fType = MFT_STRING;
	}
	else // converting into a separator
	{
		// Notes about the below macro:
		// ID_TRAY_OPEN is not set to be the default for the self-contained version,since it lacks that menu item.
		CHANGE_DEFAULT_IF_NEEDED
		mii.fType = MFT_SEPARATOR;
		if (aMenuItem->mSubmenu)  // Converting submenu into a separator.
		{
			mii.fMask |= MIIM_SUBMENU;
			mii.hSubMenu = NULL;
		}
	}

	if (SetMenuItemInfo(mMenu, aMenuItem_ID, aMenuItem->mSubmenu != NULL, &mii))
		RENAME_MENU_UPDATE
	// else no error msg and return OK so that the thread will continue.  This may help catch
	// bugs in the course of normal use of this feature.
	return OK;
}



ResultType UserMenu::CheckItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mChecked = true;
	if (mMenu)
		CheckMenuItem(mMenu, aMenuItem_ID, aMenuItem_MF_BY | MF_CHECKED);
	return OK;
}



ResultType UserMenu::UncheckItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mChecked = false;
	if (mMenu)
		CheckMenuItem(mMenu, aMenuItem_ID, aMenuItem_MF_BY | MF_UNCHECKED);
	return OK;
}



ResultType UserMenu::ToggleCheckItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mChecked = !aMenuItem->mChecked;
	if (mMenu)
		CheckMenuItem(mMenu, aMenuItem_ID, aMenuItem_MF_BY | (aMenuItem->mChecked ? MF_CHECKED : MF_UNCHECKED));
	return OK;
}



ResultType UserMenu::EnableItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mEnabled = true;
	if (mMenu)
		EnableMenuItem(mMenu, aMenuItem_ID, aMenuItem_MF_BY | MF_ENABLED); // Automatically ungrays it too.
	return OK;

}



ResultType UserMenu::DisableItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mEnabled = false;
	if (mMenu)
		EnableMenuItem(mMenu,aMenuItem_ID, aMenuItem_MF_BY | MF_DISABLED | MF_GRAYED);
	return OK;

}



ResultType UserMenu::ToggleEnableItem(UserMenuItem *aMenuItem)
{
	aMenuItem->mEnabled = !aMenuItem->mEnabled;
	if (mMenu)
		EnableMenuItem(mMenu, aMenuItem_ID, aMenuItem_MF_BY | (aMenuItem->mEnabled ? MF_ENABLED
			: (MF_DISABLED | MF_GRAYED)));
	return OK;

}



ResultType UserMenu::SetDefault(UserMenuItem *aMenuItem)
{
	if (mDefault == aMenuItem)
		return OK;
	mDefault = aMenuItem;
	if (!mMenu) // No further action required: the new setting will be in effect when the menu is created.
		return OK;
	if (aMenuItem) // A user-defined menu item is being made the default.
	{
		SetMenuDefaultItem(mMenu, aMenuItem_ID, aMenuItem->mSubmenu != NULL); // This also ensures that only one is default at a time.
		return OK;
	}
	// Otherwise, a user-defined item that was previously the default is no longer the default.
	// Provide a new default if this is the tray menu, the standard items are present, and a default
	// action is called for:
	if (this == g_script.mTrayMenu) // Necessary for proper operation of the self-contained version:
#ifdef AUTOHOTKEYSC
		SetMenuDefaultItem(mMenu, g_AllowMainWindow && mIncludeStandardItems ? ID_TRAY_OPEN : -1, FALSE);
#else
		SetMenuDefaultItem(mMenu, mIncludeStandardItems ? ID_TRAY_OPEN : -1, FALSE);
#endif
	else
		SetMenuDefaultItem(mMenu, -1, FALSE);
	return OK;

}



ResultType UserMenu::IncludeStandardItems()
{
	if (mIncludeStandardItems)
		return OK;
	// In this case, immediately create the menu to support having the standard menu items on the
	// bottom or middle rather than at the top (which is the default). Older comment: Only do
	// this if it was false beforehand so that the standard menu items will be appended to whatever
	// the user has already added to the tray menu (increases flexibility).
	if (!Create()) // It may already exist, in which case this returns OK.
		return FAIL; // No error msg since so rare.
	return AppendStandardItems();
}


ResultType UserMenu::ExcludeStandardItems()
{
	if (!mIncludeStandardItems)
		return OK;
	mIncludeStandardItems = false;
	return Destroy(); // It will be recreated automatically the next time the user displays it.
}



ResultType UserMenu::Create()
{
	if (mMenu)  // Besides making sense, this should stop runaway recursion if menus are submenus of each other.
		return OK;
	if (   !(mMenu = CreatePopupMenu())   ) // Rare, so no error msg here (caller can, if it wants).
		return FAIL;

	// It seems best not to have a mandatory EXIT item added to the bottom of the tray menu
	// for these reasons:
	// 1) Allows the tray icon to be shown even at time when the user wants it to have no menu at all
	//    (i.e. avoids the need for #NoTrayIcon just to disable the showing of the menu).
	// 2) Avoids complexity because there would be a 3rd state: Standard, NoStandard, and
	//    NoStandardWithExit.  This might be inconsequential, but would requir testing.
	//if (!mIncludeStandardItems && !mMenuItemCount)
	//{
	//	AppendMenu(mTrayMenu->mMenu, MF_STRING, ID_TRAY_EXIT, "E&xit");
	//	return OK;
	//}

	// By default, the standard menu items are added first, since the users would probably want
	// their own user defined menus at the bottom where they're easier to reach:
	if (mIncludeStandardItems)
		AppendStandardItems();

	// Now append all of the user defined items:
	UINT flags;
	UserMenuItem *mi;
	for (mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
	{
		flags = 0;
		if (!*mi->mName)
			flags |= MF_SEPARATOR;  // MF_STRING is the default.
		if (!mi->mEnabled)
			flags |= (MF_DISABLED | MF_GRAYED);  // MF_ENABLED is the default.
		if (mi->mChecked)   // MF_UNCHECKED is the default.
			flags |= MF_CHECKED;
		if (mi->mSubmenu)
		{
			flags |= MF_POPUP;
			// Ensure submenu is created so that handle can be used below.
			if (!mi->mSubmenu->Create())
				return FAIL;
		}
		AppendMenu(mMenu, flags, mi->mSubmenu ? (UINT_PTR)mi->mSubmenu->mMenu : mi->mMenuID, mi->mName);
	}
	if (mDefault)
		// This also ensures that only one is default at a time:
		SetMenuDefaultItem(mMenu, mDefault->mMenuID, FALSE);

	return OK;
}



ResultType UserMenu::AppendStandardItems()
// Caller must ensure that this->mMenu exists if it wants the items to be added immediately.
{
	mIncludeStandardItems = true; // even if the menu doesn't exist.
	if (!mMenu)
		return OK;
#ifdef AUTOHOTKEYSC
	if (g_AllowMainWindow)
	{
		AppendMenu(mMenu, MF_STRING, ID_TRAY_OPEN, "&Open");
		if (this == g_script.mTrayMenu && !mDefault) // No user-defined default menu item, so use the standard one.
			SetMenuDefaultItem(mMenu, ID_TRAY_OPEN, FALSE); // Seems to have no function other than appearance.
	}
#else
	AppendMenu(mMenu, MF_STRING, ID_TRAY_OPEN, "&Open");
	AppendMenu(mMenu, MF_STRING, ID_TRAY_HELP, "&Help");
	AppendMenu(mMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(mMenu, MF_STRING, ID_TRAY_WINDOWSPY, "&Window Spy");
	AppendMenu(mMenu, MF_STRING, ID_TRAY_RELOADSCRIPT, "&Reload This Script");
	AppendMenu(mMenu, MF_STRING, ID_TRAY_EDITSCRIPT, "&Edit This Script");
	AppendMenu(mMenu, MF_SEPARATOR, 0, NULL);
	if (this == g_script.mTrayMenu && !mDefault) // No user-defined default menu item, so use the standard one.
		SetMenuDefaultItem(mMenu, ID_TRAY_OPEN, FALSE); // Seems to have no function other than appearance.
#endif
	AppendMenu(mMenu, MF_STRING, ID_TRAY_SUSPEND, "&Suspend Hotkeys");
	AppendMenu(mMenu, MF_STRING, ID_TRAY_PAUSE, "&Pause Script");
	AppendMenu(mMenu, MF_STRING, ID_TRAY_EXIT, "E&xit");
	return OK;  // For caller convenience.
}



ResultType UserMenu::Destroy()
{
	if (!mMenu)  // Besides performance, this will halt runaway recursion if menus contain each other as submenus.
		return OK;
	// I think DestroyMenu() can fail if an attempt is made to destroy the menu while it is being
	// displayed (but even if it doesn't fail, it seems very bad to try to destroy it then, which
	// is why g_MenuIsVisible is checked just to be sure).
	// But this all should be impossible in our case because the script is in an uninterruptible state
	// while the menu is displayed, which in addition to pausing the current thread (which happens
	// anyway), no new timed or hotkey subroutines can be launched.  Thus, this should rarely if
	// ever happen, which is why no error message is given here:
	//if (g_MenuIsVisible)
	//	return FAIL;

	// DestroyMenu fails (GetLastError() == ERROR_INVALID_MENU_HANDLE) if a parent menu that contained
	// mMenu as one of its submenus was destroyed above.  This seems to indicate that submenus are
	// destroyed whenever a parent menu is destroyed.  Therefore, don't check failure on the below,
	// just assume that afterward, the menu is gone.  IsMenu() is checked because the handle can be
	// invalid if the OS already destroyed it behind-the-scenes (this happens to a submenu whenever
	// its parent menu is destroyed, or whenever a submenu is converted back into a normal menu item):
	if (IsMenu(mMenu)) // In addition to making sense, this check should prevent runaway recursion.
	{
		// Doing this first should prevent the recursive calls to Destroy() below from causing
		// infinite recursion of menus are submenus of each other:
		DestroyMenu(mMenu);
		// The moment the above is done, any submenus that were attached to mMenu are also destroyed
		// by the OS.  So mark them as destroyed in our bookkeeping also:
		UserMenuItem *mi;
		for (mi = mFirstMenuItem; mi ; mi = mi->mNextMenuItem)
			if (mi->mSubmenu && mi->mSubmenu->mMenu && !IsMenu(mi->mSubmenu->mMenu))
				mi->mSubmenu->Destroy();

		// Destroy any menu that contains this menu as a submenu.  This is done so that such
		// menus will be automatically recreated the next time they are used, which is necessary
		// because otherwise when such a menu is displayed the next time, the OS will show its
		// old contents even though the menu is gone.  Thus, those old menu items will be
		// selectable but will have no effect.  In addition, sometimes our caller plans to
		// recreate this->mMenu (or have it recreated automatically upon first use) and thus
		// we don't want to use DeleteMenu() because that would require having to detect whether
		// the menu needs updating (to reflect whether the submenu has been recreated) every
		// time we display it.  Another drawback to DeleteMenu() is that it would changing the
		// order of the menu items to something other than what the user originally specified
		// unless InsertMenu was used during the update:
		for (UserMenu *m = g_script.mFirstMenu; m; m = m->mNextMenu)
			if (m->mMenu)
				for (mi = m->mFirstMenuItem; mi; mi = mi->mNextMenuItem)
					if (mi->mSubmenu == this)
						m->Destroy();  // Destroy any menu that contains this menu as a submenu.

	}
	mMenu = NULL;
	return OK;
}



ResultType UserMenu::Display(bool aForceToForeground)
// aForceToForeground defaults to true because when a menu is displayed spontanesouly rather than
// in response to the user right-clicking the tray icon, I believe that the OS will revert to its
// behavior of "resisting" a window that tries to "steal focus".  I believe this resistance does
// not occur when the user clicks the icon because that click causes the task bar to get focus,
// and it is likely that the OS allows other windows to steal focus from the task bar without
// resistence.  This is done because if the main window is *not* successfully activated prior to
// displaying the menu, it might be impossible to dismiss the menu by clicking outside of it.
{
	if (!mMenuItemCount && !mIncludeStandardItems)
		return OK;  // Consider the display of an empty menu to be a success.
	if (!mMenu) // i.e. because this is the first time the user has opened the menu.
		if (!Create()) // no error msg since so rare
			return FAIL;
	if (this == g_script.mTrayMenu)
	{
		// These are okay even if the menu items don't exist (perhaps because the user customized the menu):
		CheckMenuItem(mMenu, ID_TRAY_SUSPEND, g_IsSuspended ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(mMenu, ID_TRAY_PAUSE, g.IsPaused ? MF_CHECKED : MF_UNCHECKED);
	}
	POINT pt;
	GetCursorPos(&pt);
	// Always bring main window to foreground right before TrackPopupMenu(), even if window is hidden.
	// UPDATE: This is a problem because SetForegroundWindowEx() will restore the window if it's hidden,
	// but restoring also shows the window if it's hidden.  Could re-hide it... but the question here
	// is can a minimized window be the foreground window?  If not, how to explain why
	// SetForegroundWindow() always seems to work for the purpose of the tray menu?
	//if (aForceToForeground)
	//{
	//	// Seems best to avoid using the script's current setting of #WinActivateForce.  Instead, always
	//	// try the gentle approach first since it is unlikely that displaying a menu will cause the
	//	// "flashing task bar button" problem?
	//	bool original_setting = g_WinActivateForce;
	//	g_WinActivateForce = false;
	//	SetForegroundWindowEx(g_hWnd);
	//	g_WinActivateForce = original_setting;
	//}
	//else
		SetForegroundWindow(g_hWnd);
	g_MenuIsVisible = MENU_VISIBLE_POPUP;
	TrackPopupMenuEx(mMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, g_hWnd, NULL);
	g_MenuIsVisible = MENU_VISIBLE_NONE;
	// MSDN recommends this to prevent menu from closing on 2nd click.  It *might* only apply to when
	// the menu is opened via the tray icon, but not knowing, it seems safe to do it unconditionally:
	PostMessage(g_hWnd, WM_NULL, 0, 0);
	return OK;
}



UINT UserMenu::GetSubmenuPos(HMENU ahMenu)
// ahMenu will be searched for in this->mMenu.
// Returns UINT_MAX if this->mMenu is NULL, ahMenu is NULL, or if ahMenu can't be found in this->mMenu.
// Testing shows that neither ModifyMenu() nor SetMenuItemInfo() nor any other Menu modifying
// API call will accept aMenuItem->mMenuID or aMenuItem->mSubmenu as a way to uniquely
// indentify the menu item we want to change (even though GetMenuItemInfo indicates that
// aMenuItem->mSubmenu is the "ID" of sorts).  Thus, the menu item can only be modified
// by position.  Rather than having to maintain each submenu's position in every menu, thus
// making the code less maintainable since you always have to worry about standard menu items
// being added to the top vs. bottom of the menu, other menu items being inserted via
// InsertMenu() (if that is ever allowed), etc. -- just loop through the menu to find the
// right item, then return that to the caller so that it can modify the submenu based on position:
{
	if (!ahMenu || !mMenu)
		return UINT_MAX;
	int menu_item_count = GetMenuItemCount(mMenu);
	for (int i = 0; i < menu_item_count; ++i)
		if (GetSubMenu(mMenu, i) == ahMenu)
			return i;
	return UINT_MAX;
}



bool UserMenu::ContainsMenu(UserMenu *aMenu)
{
	if (!aMenu)
		return false;
	// For each submenu in mMenu: Check if it or any of its submenus equals aMenu.
	for (UserMenuItem *mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
		if (mi->mSubmenu)
			if (mi->mSubmenu == aMenu || mi->mSubmenu->ContainsMenu(aMenu)) // recursive
				return true;
			//else keep searching
	return false;
}