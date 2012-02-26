#include "IGraphicsCarbon.h"
#ifndef IPLUG_NO_CARBON_SUPPORT

IRECT GetRegionRect(EventRef pEvent, int gfxW, int gfxH)
{
  RgnHandle pRgn = 0;
  if (GetEventParameter(pEvent, kEventParamRgnHandle, typeQDRgnHandle, 0, sizeof(RgnHandle), 0, &pRgn) == noErr && pRgn) {
    Rect rct;
    GetRegionBounds(pRgn, &rct);
    return IRECT(rct.left, rct.top, rct.right, rct.bottom); 
  }
  return IRECT(0, 0, gfxW, gfxH);
}

IRECT GetControlRect(EventRef pEvent, int gfxW, int gfxH)
{
  Rect rct;
  if (GetEventParameter(pEvent, kEventParamCurrentBounds, typeQDRectangle, 0, sizeof(Rect), 0, &rct) == noErr) {
    int w = rct.right - rct.left;
    int h = rct.bottom - rct.top;
    if (w > 0 && h > 0) {
      return IRECT(0, 0, w, h);
    }
  }  
  return IRECT(0, 0, gfxW, gfxH);
}

void ResizeWindow(WindowRef pWindow, int w, int h)
{
  Rect gr;  // Screen.
  GetWindowBounds(pWindow, kWindowContentRgn, &gr);
  gr.right = gr.left + w;
  gr.bottom = gr.top + h;
  SetWindowBounds(pWindow, kWindowContentRgn, &gr); 
}

IGraphicsCarbon::IGraphicsCarbon(IGraphicsMac* pGraphicsMac, WindowRef pWindow, ControlRef pParentControl)
: mGraphicsMac(pGraphicsMac)
, mWindow(pWindow)
, mView(0)
, mTimer(0)
, mControlHandler(0)
, mWindowHandler(0)
, mCGC(0)
, mTextEntryView(0)
, mTextEntryHandler(0)
, mEdControl(0)
, mEdParam(0)
, mPrevX(0)
, mPrevY(0)
, mRgn(NewRgn())
{ 
  TRACE;
  
  Rect r;   // Client.
  r.left = r.top = 0;
  r.right = pGraphicsMac->Width();
  r.bottom = pGraphicsMac->Height();   
  
  WindowAttributes winAttrs = 0;
  GetWindowAttributes(pWindow, &winAttrs);
  mIsComposited = (winAttrs & kWindowCompositingAttribute);
 
  UInt32 features =  kControlSupportsFocus | kControlHandlesTracking | kControlSupportsEmbedding;
  
  if (mIsComposited) 
  {
    features |= kHIViewIsOpaque | kHIViewFeatureDoesNotUseSpecialParts;
  }
  
  CreateUserPaneControl(pWindow, &r, features, &mView);    
  
  const EventTypeSpec controlEvents[] = 
  {	
    { kEventClassControl, kEventControlDraw },
  };
  
  InstallControlEventHandler(mView, MainEventHandler, GetEventTypeCount(controlEvents), controlEvents, this, &mControlHandler);
  
  const EventTypeSpec windowEvents[] = 
  {
    { kEventClassMouse, kEventMouseDown },
    { kEventClassMouse, kEventMouseUp },
    { kEventClassMouse, kEventMouseMoved },
    { kEventClassMouse, kEventMouseDragged },
    { kEventClassMouse, kEventMouseWheelMoved },
    
    { kEventClassKeyboard, kEventRawKeyDown },
    
    { kEventClassWindow, kEventWindowDeactivated }
  };
  
  InstallWindowEventHandler(mWindow, MainEventHandler, GetEventTypeCount(windowEvents), windowEvents, this, &mWindowHandler);  
  
  double t = kEventDurationSecond / (double) pGraphicsMac->FPS();
  
  OSStatus s = InstallEventLoopTimer(GetMainEventLoop(), 0., t, TimerHandler, this, &mTimer);
  
  if (mIsComposited) 
  {
    if (!pParentControl) 
    {
      HIViewRef hvRoot = HIViewGetRoot(pWindow);
      s = HIViewFindByID(hvRoot, kHIViewWindowContentID, &pParentControl); 
    }
    
    s = HIViewAddSubview(pParentControl, mView);
  }
  else 
  {
    if (!pParentControl) 
    {
      if (GetRootControl(pWindow, &pParentControl) != noErr) 
      {
        CreateRootControl(pWindow, &pParentControl);
      }
    }
    s = EmbedControl(mView, pParentControl); 
  }

  if (s == noErr) 
  {
    SizeControl(mView, r.right, r.bottom);  // offset?
  }
}

IGraphicsCarbon::~IGraphicsCarbon()
{
  if (mTextEntryView)
  {
    RemoveEventHandler(mTextEntryHandler);
    mTextEntryHandler = 0;
    HIViewRemoveFromSuperview(mTextEntryView);
    mTextEntryView = 0;
    mEdControl = 0;
    mEdParam = 0;
  }
  
  RemoveEventLoopTimer(mTimer);
  RemoveEventHandler(mControlHandler);
  RemoveEventHandler(mWindowHandler);
  mTimer = 0;
  mView = 0;
  DisposeRgn(mRgn);
}

bool IGraphicsCarbon::Resize(int w, int h)
{
  if (mWindow && mView) 
  {
    ResizeWindow(mWindow, w, h);
    CGRect tmp = CGRectMake(0, 0, w, h);
    return (HIViewSetFrame(mView, &tmp) == noErr);
  }
  return false;
}

void IGraphicsCarbon::EndUserInput(bool commit)
{
  RemoveEventHandler(mTextEntryHandler);
  mTextEntryHandler = 0;

  if (commit)
  {
    CFStringRef str;
    if (GetControlData(mTextEntryView, kControlEditTextPart, kControlEditTextCFStringTag, sizeof(str), &str, NULL) == noErr)
    {
      char txt[MAX_PARAM_LEN];
      CFStringGetCString(str, txt, MAX_PARAM_LEN, kCFStringEncodingUTF8);
      CFRelease(str);
      
      if (mEdParam) 
        mGraphicsMac->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);
      else
        mEdControl->TextFromTextEntry(txt);
    }
  }

  HIViewSetVisible(mTextEntryView, false);
  HIViewRemoveFromSuperview(mTextEntryView);
  
  if (mIsComposited)
  {
    HIViewSetNeedsDisplay(mView, true);
  }
  else
  {
    mEdControl->SetDirty(false);
    mEdControl->Redraw();
  }
  SetThemeCursor(kThemeArrowCursor);
  SetUserFocusWindow(kUserFocusAuto);

  mTextEntryView = 0;
  mEdControl = 0;
  mEdParam = 0;
}

IPopupMenu* IGraphicsCarbon::CreateIPopupMenu(IPopupMenu* pMenu, IRECT* pAreaRect)
{
	MenuRef menuRef = 0;
	ResID menuID = UniqueID ('MENU');

	int numItems = pMenu->GetNItems();
	
	if (numItems && CreateNewMenu(menuID, kMenuAttrCondenseSeparators, &menuRef) == noErr)
	{
//		bool multipleCheck = menu->getStyle () & (kMultipleCheckStyle & ~kCheckStyle);

		for (int i = 0; i < numItems; ++i)
		{	
			IPopupMenuItem* menuItem = pMenu->GetItem(i);
			
			if (menuItem->GetIsSeparator())
				AppendMenuItemTextWithCFString(menuRef, CFSTR(""), kMenuItemAttrSeparator, 0, NULL);
			else
			{
				CFStringRef itemString = CFStringCreateWithCString (NULL, menuItem->GetText(), kCFStringEncodingUTF8);
				
				if (pMenu->GetPrefix())
				{
					CFStringRef prefixString = 0;
					
					switch (pMenu->GetPrefix())
					{
						case 0: 
							prefixString = CFStringCreateWithFormat (NULL, 0, CFSTR(""),i+1); break;
						case 1:
							prefixString = CFStringCreateWithFormat (NULL, 0, CFSTR("%1d: "),i+1); break;
						case 2:
							prefixString = CFStringCreateWithFormat (NULL, 0, CFSTR("%02d: "),i+1); break;
						case 3:
							prefixString = CFStringCreateWithFormat (NULL, 0, CFSTR("%03d: "),i+1); break;
					}
					
					CFMutableStringRef newItemString = CFStringCreateMutable (0, 0);
					CFStringAppend (newItemString, prefixString);
					CFStringAppend (newItemString, itemString);
					CFRelease (itemString);
					CFRelease (prefixString);
					itemString = newItemString;
				}
				
				if (itemString == 0)
					continue;
				
				MenuItemAttributes itemAttribs = kMenuItemAttrIgnoreMeta;
				if (!menuItem->GetEnabled())
					itemAttribs |= kMenuItemAttrDisabled;
				if (menuItem->GetIsTitle())
					itemAttribs |= kMenuItemAttrSectionHeader;
				
				InsertMenuItemTextWithCFString(menuRef, itemString, i, itemAttribs, 0);
								
				if (menuItem->GetChecked())
				{
					//MacCheckMenuItem(menuRef, i, true);
					CheckMenuItem(menuRef, i+1, true);
				}
//				if (menuItem->GetChecked() && multipleCheck)
//					CheckMenuItem (menuRef, i, true);
				
//				if (menuItem->GetSubmenu())
//				{
//					MenuRef submenu = createMenu (menuItem->GetSubmenu());
//					if (submenu)
//					{
//						SetMenuItemHierarchicalMenu (menuRef, i, submenu);
//						CFRelease (submenu);
//					}
//				}
								
				CFRelease (itemString);
			}
		}
		
	//	if (pMenu->getStyle() & kCheckStyle && !multipleCheck)
	//		CheckMenuItem (menuRef, pMenu->getCurrentIndex (true) + 1, true);
	//	SetMenuItemRefCon(menuRef, 0, (int32_t)menu);
	//	InsertMenu(menuRef, kInsertHierarchicalMenu);
	}

	if (menuRef)
	{
		CalcMenuSize(menuRef);
	//	SInt16 menuWidth = GetMenuWidth(menuRef);
	//	if (menuWidth < optionMenu->getViewSize().getWidth())
	//		SetMenuWidth(menuRef, optionMenu->getViewSize().getWidth());
	//	int32_t popUpItem = 1;
	//	int32_t PopUpMenuItem = PopUpMenuItem = PopUpMenuSelect(menuRef, gy, gx, popUpItem);
		
    // Get the plugin gui frame rect within the host's window
    HIRect rct;
    HIViewGetFrame(this->mView, &rct);
    
    // Get the host's window rect within the screen
    Rect wrct;
    GetWindowBounds(this->mWindow, kWindowContentRgn, &wrct);

    HIViewRef contentView;
    HIViewFindByID (HIViewGetRoot(this->mWindow), kHIViewWindowContentID, &contentView);
    HIViewConvertRect(&rct, HIViewGetSuperview((HIViewRef)this->mView), contentView);
    
    int xpos = wrct.left + rct.origin.x + pAreaRect->L;
    int ypos = wrct.top + rct.origin.y + pAreaRect->B + 5;
    
    int32_t PopUpMenuItem = PopUpMenuSelect (menuRef, ypos, xpos, 0);//popUpItem);

		short result = LoWord(PopUpMenuItem) - 1;	
		short menuIDResult = HiWord(PopUpMenuItem);
		IPopupMenu* resultMenu = 0;
		
		if (menuIDResult != 0)
		{
			//MenuRef usedMenuRef = GetMenuHandle(menuIDResult);
			
			//if (usedMenuRef)
			//{
			//	if (GetMenuItemRefCon(usedMenuRef, 0, (URefCon*)&resultMenu) == noErr)
			//	{
				//	popupResult.menu = resultMenu;
				//	popupResult.index = result;
					
					//printf("result = %i", result);
					
					resultMenu = pMenu;
					resultMenu->SetChosenItemIdx(result);
			//	}
			//}
		}
		
		CFRelease(menuRef);
		
		return resultMenu;
	}
	else {
		return 0;
	}
}

void IGraphicsCarbon::CreateTextEntry(IControl* pControl, IText* pText, IRECT* pTextRect, const char* pString, IParam* pParam)
{
  ControlRef control = 0;

	if (!pControl || mTextEntryView || !mIsComposited) return; // Only composited carbon supports text entry
  
  Rect r = { pTextRect->T, pTextRect->L, pTextRect->B, pTextRect->R }; // these adjustments should make it the same as the cocoa one

  if (CreateEditUnicodeTextControl(NULL, &r, NULL, false, NULL, &control) != noErr) return;
  
  HIViewAddSubview(mView, control);
  
  const EventTypeSpec events[] = 
  {
    { kEventClassKeyboard, kEventRawKeyDown },
    { kEventClassKeyboard, kEventRawKeyRepeat }
    //,{ kEventClassControl, kEventControlDraw }
  };
  
  InstallControlEventHandler(control, TextEntryHandler, GetEventTypeCount(events), events, this, &mTextEntryHandler);
  mTextEntryView = control;
  
  if (pString[0] != '\0')
  {
    CFStringRef str = CFStringCreateWithCString(NULL, pString, kCFStringEncodingUTF8);
    
    if (str)
    {
      SetControlData(mTextEntryView, kControlEditTextPart, kControlEditTextCFStringTag, sizeof(str), &str);
      CFRelease(str);
    }
    
    ControlEditTextSelectionRec sel;
    sel.selStart = 0;
    sel.selEnd = strlen(pString);
    SetControlData(mTextEntryView, kControlEditTextPart, kControlEditTextSelectionTag, sizeof(sel), &sel);
  }
  
  // not multiline
	Boolean value = true;
	SetControlData(mTextEntryView, kControlEditTextPart, kControlEditTextSingleLineTag, sizeof(value), (void *)&value);
  
  int align = 0;
  
  switch ( pText->mAlign ) 
  {
    case IText::kAlignNear:
      align = teJustLeft;
      break;
    case IText::kAlignCenter:
      align = teCenter;
      break;
    case IText::kAlignFar:
      align = teJustRight;
      break;
    default:
      align = teCenter;
      break;
  }	

  ControlFontStyleRec font = { kControlUseJustMask | kControlUseSizeMask | kControlUseFontMask, 0, pText->mSize, 0, 0, align, 0, 0 };
  CFStringRef str = CFStringCreateWithCString(NULL, pText->mFont, kCFStringEncodingUTF8);
  font.font = ATSFontFamilyFindFromName(str, kATSOptionFlagsDefault);
  SetControlData(mTextEntryView, kControlEditTextPart, kControlFontStyleTag, sizeof(font), &font);
  CFRelease(str);

  HIViewSetVisible(mTextEntryView, true);
  HIViewAdvanceFocus(mTextEntryView, 0);
  SetKeyboardFocus(mWindow, mTextEntryView, kControlEditTextPart);
  SetUserFocusWindow(mWindow);
  
  mEdControl = pControl;
  mEdParam = pParam;
}

// static 
pascal OSStatus IGraphicsCarbon::MainEventHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  IGraphicsMac* pGraphicsMac = _this->mGraphicsMac;
  UInt32 eventClass = GetEventClass(pEvent);
  UInt32 eventKind = GetEventKind(pEvent);
  
  switch (eventClass) {
    case kEventClassKeyboard:
    { 
      switch (eventKind) { 
        case kEventRawKeyDown:{
          
          if (_this->mTextEntryView)
            return eventNotHandledErr;
          
          bool handle = true;
          int key;     
          UInt32 k;
          GetEventParameter(pEvent, kEventParamKeyCode, typeUInt32, NULL, sizeof(UInt32), NULL, &k);
          
          char c;
          GetEventParameter(pEvent, kEventParamKeyMacCharCodes, typeChar, NULL, sizeof(char), NULL, &c);
          
          if (k == 49) key = KEY_SPACE;
          else if (k == 125) key = KEY_DOWNARROW;
          else if (k == 126) key = KEY_UPARROW;
          else if (k == 123) key = KEY_LEFTARROW;
          else if (k == 124) key = KEY_RIGHTARROW;
          else if (c >= '0' && c <= '9') key = KEY_DIGIT_0+c-'0';
          else if (c >= 'A' && c <= 'Z') key = KEY_ALPHA_A+c-'A';
          else if (c >= 'a' && c <= 'z') key = KEY_ALPHA_A+c-'a';
          else handle = false;
          
          if(handle)
            handle = pGraphicsMac->OnKeyDown(_this->mPrevX, _this->mPrevY, key);
          
          if(handle)
            return noErr;
          else
            return eventNotHandledErr;
          
        } 
      }
    }
    case kEventClassControl: 
    {
      switch (eventKind) 
      {          
        case kEventControlDraw: 
        {
          int gfxW = pGraphicsMac->Width(), gfxH = pGraphicsMac->Height();
          
          IRECT r = GetRegionRect(pEvent, gfxW, gfxH);  
          
          CGrafPtr port = 0;
          
          if (_this->mIsComposited) 
          {
            GetEventParameter(pEvent, kEventParamCGContextRef, typeCGContextRef, 0, sizeof(CGContextRef), 0, &(_this->mCGC));         
            CGContextTranslateCTM(_this->mCGC, 0, gfxH);
            CGContextScaleCTM(_this->mCGC, 1.0, -1.0);     
            pGraphicsMac->Draw(&r);
          }
          else
					{
						GetEventParameter(pEvent, kEventParamGrafPort, typeGrafPtr, 0, sizeof(CGrafPtr), 0, &port);
						QDBeginCGContext(port, &(_this->mCGC));
						
						RgnHandle clipRegion = NewRgn();
						GetPortClipRegion(port, clipRegion);
						
						Rect portBounds;
						GetPortBounds(port, &portBounds);
						
						int offsetW = 0;
            
						if ((portBounds.right - portBounds.left) >= gfxW)
						{
							offsetW = 0.5 * ((portBounds.right - portBounds.left) - gfxW);
						}
            
						CGContextTranslateCTM(_this->mCGC, portBounds.left + offsetW, -portBounds.top);
						
						r.L = r.T = r.R = r.B = 0;
						pGraphicsMac->IsDirty(&r);
						pGraphicsMac->Draw(&r);
						
            //CGContextFlush(_this->mCGC);
						QDEndCGContext(port, &(_this->mCGC));						
					}      
          return noErr;
        }
      }
      break;
    }
    case kEventClassMouse: 
    {
      HIPoint hp;
      GetEventParameter(pEvent, kEventParamWindowMouseLocation, typeHIPoint, 0, sizeof(HIPoint), 0, &hp);
      HIPointConvert(&hp, kHICoordSpaceWindow, _this->mWindow, kHICoordSpaceView, _this->mView);
      int x = (int) hp.x - 2;
      int y = (int) hp.y - 3;
      
      UInt32 mods;
      GetEventParameter(pEvent, kEventParamKeyModifiers, typeUInt32, 0, sizeof(UInt32), 0, &mods);
      EventMouseButton button;
      GetEventParameter(pEvent, kEventParamMouseButton, typeMouseButton, 0, sizeof(EventMouseButton), 0, &button);
      if (button == kEventMouseButtonPrimary && (mods & cmdKey)) button = kEventMouseButtonSecondary;
      IMouseMod mmod(true, button == kEventMouseButtonSecondary, (mods & shiftKey), (mods & controlKey), (mods & optionKey));
      
      switch (eventKind) 
      {
        case kEventMouseDown: 
        {
          if (_this->mTextEntryView)
          {
            HIViewRef view;
            HIViewGetViewForMouseEvent(_this->mView, pEvent, &view);
            
            if (view == _this->mTextEntryView)
              return eventNotHandledErr;
            
            _this->EndUserInput(true);
          }
          
          #ifdef RTAS_API // RTAS triple click
          if (mmod.L && mmod.R && mmod.C && (pGraphicsMac->GetParamIdxForPTAutomation(x, y) > -1)) 
          {
            return CallNextEventHandler(pHandlerCall, pEvent);
          }
          #endif
          
          CallNextEventHandler(pHandlerCall, pEvent);
          
          UInt32 clickCount = 0;
          GetEventParameter(pEvent, kEventParamClickCount, typeUInt32, 0, sizeof(UInt32), 0, &clickCount);
          
          if (clickCount > 1) 
          {
            pGraphicsMac->OnMouseDblClick(x, y, &mmod);
          }
          else
          {        
            pGraphicsMac->OnMouseDown(x, y, &mmod);
          }
          
          return noErr;
        }
          
        case kEventMouseUp: 
        {
          pGraphicsMac->OnMouseUp(x, y, &mmod);
          return noErr;
        }
          
        case kEventMouseMoved: 
        {
          _this->mPrevX = x;
          _this->mPrevY = y;
          pGraphicsMac->OnMouseOver(x, y, &mmod);
          return noErr;
        }
          
        case kEventMouseDragged: 
        {
          if (!_this->mTextEntryView)
            pGraphicsMac->OnMouseDrag(x, y, &mmod);
          return noErr; 
        }
          
        case kEventMouseWheelMoved: 
        {
          EventMouseWheelAxis axis;
          GetEventParameter(pEvent, kEventParamMouseWheelAxis, typeMouseWheelAxis, 0, sizeof(EventMouseWheelAxis), 0, &axis);
          
          if (axis == kEventMouseWheelAxisY) 
          {
            int d;
            GetEventParameter(pEvent, kEventParamMouseWheelDelta, typeSInt32, 0, sizeof(SInt32), 0, &d);
            
            if (_this->mTextEntryView) _this->EndUserInput(false);
            
            pGraphicsMac->OnMouseWheel(x, y, &mmod, d);
            return noErr;
          }
        }   
      }
      
      break;    
    }
      
    case kEventClassWindow:
    {
      WindowRef window;
      
      if (GetEventParameter (pEvent, kEventParamDirectObject, typeWindowRef, NULL, sizeof (WindowRef), NULL, &window) != noErr)
        break;
      
      switch (eventKind)
      {
        case kEventWindowDeactivated:
        {          
          if (_this->mTextEntryView) 
            _this->EndUserInput(false);          
          break;
        }
      }
      break;
    }
  }
  
  return eventNotHandledErr;
}    

// static 
pascal void IGraphicsCarbon::TimerHandler(EventLoopTimerRef pTimer, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  
  IRECT r;
  
  if (_this->mGraphicsMac->IsDirty(&r)) 
  {
    if (_this->mIsComposited) 
    {
      CGRect tmp = CGRectMake(r.L, r.T, r.W(), r.H());
      HIViewSetNeedsDisplayInRect(_this->mView, &tmp , true);
    }
    else 
    {
      UpdateControls(_this->mWindow, 0);
    }
  } 
}

// static
pascal OSStatus IGraphicsCarbon::TextEntryHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  UInt32 eventClass = GetEventClass(pEvent);
  UInt32 eventKind = GetEventKind(pEvent);
  
  switch (eventClass)
  {
      //    case kEventClassControl:
      //		{
      //			switch (eventKind)
      //			{
      //				case kEventControlDraw:
      //				{
      //          // todo... maybe
      //          return noErr;
      //        }
      //      }
      //    }
      
    case kEventClassKeyboard:
    {
      switch (eventKind)
      {
        case kEventRawKeyDown:
        case kEventRawKeyRepeat:
        {
          char c;
          GetEventParameter(pEvent, kEventParamKeyMacCharCodes, typeChar, NULL, sizeof(c), NULL, &c);
          UInt32 k;
          GetEventParameter(pEvent, kEventParamKeyCode, typeUInt32, NULL, sizeof(UInt32), NULL, &k);
          
          // trap enter key
          if (c == 3 || c == 13) {
            _this->EndUserInput(true);
            return noErr;
          }
          
          // pass arrow keys
          if (k == 125 || k == 126 || k == 123 || k == 124) 
            break;
          
          // pass delete keys
          if (c == 8 || c == 127)
            break;
          
          if (_this->mEdParam) {
            switch ( _this->mEdParam->Type() ) 
            {
              case IParam::kTypeEnum:
              case IParam::kTypeInt:
              case IParam::kTypeBool:
                if (c >= '0' && c <= '9') break;
                else if (c == '-') break;
                else if (c == '+') break;
                else return noErr;
              case IParam::kTypeDouble:
                if (c >= '0' && c <= '9') break;
                else if (c == '.') break;
                else if (c == '-') break;
                else if (c == '+') break;
                else return noErr;
              default:
                break;
            }
          }
          
          Str31  theText; // TODO: check the actually limit here... 32 chars enough?
          Size   textLength;            
          
          GetControlData(_this->mTextEntryView,
                         kControlNoPart,
                         kControlEditTextTextTag,
                         sizeof(theText) -1,
                         (Ptr) &theText[1],
                         &textLength);
          
          if(textLength >= _this->mEdControl->GetTextEntryLength())
            return noErr;
          
          break;
        }
      }
      break;
    }
  }
  return eventNotHandledErr;
}

#endif // IPLUG_NO_CARBON_SUPPORT
