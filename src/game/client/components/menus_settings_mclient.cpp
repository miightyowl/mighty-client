/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"
#include "skins.h"

#include <base/dbg.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/external/json-parser/json.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>
#include <memory>

void CMenus::RenderSettingsMClient(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = 30.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 20.0f;
	const float ColorPickerLabelSize = 13.0f;

	CUIRect TabBar, LeftView, RightView, Button, Label;

	// sub category tab
	MainView.HSplitTop(24.0f, &TabBar, &MainView);

	enum
	{
		MCLIENT_TAB_GAMEPLAY = 0,
		MCLIENT_TAB_FROZEN,
		MCLIENT_TAB_APPEARANCE,
		NUM_MCLIENT_TABS,
	};
	static int s_CurTab = MCLIENT_TAB_GAMEPLAY;
	const char *apTabNames[NUM_MCLIENT_TABS] = {
		Localize("Gameplay"),
		Localize("Frozen"),
		Localize("Appearance")};
	static CButtonContainer s_aTabButtons[NUM_MCLIENT_TABS];
	CUIRect TabRow = TabBar;
	for(int Tab = 0; Tab < NUM_MCLIENT_TABS; ++Tab)
	{
		const float LabelW = TextRender()->TextWidth(12.0f, apTabNames[Tab]);
		TabRow.VSplitLeft(LabelW + 22.0f, &Button, &TabRow);
		const bool Active = s_CurTab == Tab;
		const bool Hovered = Ui()->HotItem() == &s_aTabButtons[Tab];
		CUIRect TabLabel, Underline;
		Button.HSplitBottom(2.0f, &TabLabel, &Underline);
		SLabelProperties Props;
		Props.SetColor(Active ? ColorRGBA(0.96f, 0.96f, 0.96f, 1.0f) : (Hovered ? ColorRGBA(0.80f, 0.80f, 0.80f, 1.0f) : ColorRGBA(0.50f, 0.50f, 0.50f, 1.0f)));
		Ui()->DoLabel(&TabLabel, apTabNames[Tab], 12.0f, TEXTALIGN_MC, Props);
		if(Active)
			Underline.Draw(AccentColor().WithAlpha(1.0f), IGraphics::CORNER_T, 1.0f);
		if(Ui()->DoButtonLogic(&s_aTabButtons[Tab], 0, &Button, BUTTONFLAG_LEFT))
			s_CurTab = Tab;
	}

	CUIRect SubTabDivider;
	MainView.HSplitTop(1.0f, &SubTabDivider, &MainView);
	SubTabDivider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_NONE, 0.0f);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	if(s_CurTab == MCLIENT_TAB_GAMEPLAY)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// gameplay
		Ui()->DoLabel_AutoLineSize(Localize("Gameplay"), HeadlineFontSize, TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFoeAnonymize, Localize("Fully anonymize tees on your foe list"), &g_Config.m_ClFoeAnonymize, &LeftView, LineSize);
		if(g_Config.m_ClFoeAnonymize)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFoeAnonymizeRealNamesInChat, Localize("Keep real foe names in chat messages of others"), &g_Config.m_ClFoeAnonymizeRealNamesInChat, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFriendsCommunityFilter, Localize("Only show friends in enabled communities"), &g_Config.m_ClMClientFriendsCommunityFilter, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFinishFireworks, Localize("Play fireworks when you finish"), &g_Config.m_ClMClientFinishFireworks, &LeftView, LineSize);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFastInput, Localize("Fast input (lower input delay)"), &g_Config.m_ClMClientFastInput, &LeftView, LineSize);
		if(g_Config.m_ClMClientFastInput)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFastInputOthers, Localize("Apply fast input to other tees"), &g_Config.m_ClMClientFastInputOthers, &LeftView, LineSize);

		// fun
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Fun"), HeadlineFontSize, TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientForceSkin, Localize("Force maodie skin on everyone"), &g_Config.m_ClMClientForceSkin, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientPet, Localize("Show petting hand"), &g_Config.m_ClMClientPet, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientAds, Localize("Random advertisement/quote pop-ups"), &g_Config.m_ClMClientAds, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFatChat, Localize("Fat skins when someone writes \"fat\""), &g_Config.m_ClMClientFatChat, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientMaodieWalk, Localize("Giant maodie walks across the screen"), &g_Config.m_ClMClientMaodieWalk, &LeftView, LineSize);

		// experimental
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Experimental"), HeadlineFontSize, TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFinishRename, Localize("Rename near finish if name has finished"), &g_Config.m_ClFinishRename, &LeftView, LineSize);
		if(g_Config.m_ClFinishRename)
		{
			CUIRect NamesLabel;
			LeftView.HSplitTop(LineSize, &Button, &LeftView);
			Button.VSplitLeft(140.0f, &NamesLabel, &Button);
			Ui()->DoLabel(&NamesLabel, Localize("Alternative names:"), ColorPickerLabelSize, TEXTALIGN_ML);
			static CLineInput s_FinishRenameNamesInput;
			s_FinishRenameNamesInput.SetBuffer(g_Config.m_ClFinishRenameNames, sizeof(g_Config.m_ClFinishRenameNames));
			s_FinishRenameNamesInput.SetEmptyText("name1, name2, \xe2\x80\xa6");
			Ui()->DoEditBox(&s_FinishRenameNamesInput, &Button, 12.0f);
		}

		// companion pet
		Ui()->DoLabel_AutoLineSize(Localize("Companion pet"), HeadlineFontSize, TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientPetTee, Localize("Show companion tee (pet)"), &g_Config.m_ClMClientPetTee, &RightView, LineSize);
		if(g_Config.m_ClMClientPetTee)
		{
			RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientPetTeeSize, &g_Config.m_ClMClientPetTeeSize, &Button, Localize("Companion tee size"), 10, 500, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
			RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientPetTeeAlpha, &g_Config.m_ClMClientPetTeeAlpha, &Button, Localize("Companion tee opacity"), 10, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
		}
		RightView.HSplitTop(LineSize, &Label, &RightView);
		TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
		Ui()->DoLabel(&Label, Localize("Pick the companion skin in the Tee settings tab."), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		// chat translation
		RightView.HSplitTop(MarginBetweenViews, nullptr, &RightView);
		Ui()->DoLabel_AutoLineSize(Localize("Chat translation"), HeadlineFontSize, TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatTranslate, Localize("Translate incoming chat"), &g_Config.m_ClChatTranslate, &RightView, LineSize);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatTranslateShowLang, Localize("Show detected language after translated messages"), &g_Config.m_ClChatTranslateShowLang, &RightView, LineSize))
			GameClient()->m_Chat.RebuildChat();

		static const char *s_apTranslateLangCodes[] = {
			"en", "de", "es", "fr", "pt", "it", "nl", "pl", "ru", "uk", "tr", "ar",
			"zh-CN", "ja", "ko", "vi", "id", "th", "sv", "cs", "el", "hu", "ro", "fi", "da", "no", "sl", "sk", "hr", "sr", "bg"};
		static const char *s_apTranslateLangNames[] = {
			"English", "German", "Spanish", "French", "Portuguese", "Italian", "Dutch", "Polish", "Russian", "Ukrainian", "Turkish", "Arabic",
			"Chinese", "Japanese", "Korean", "Vietnamese", "Indonesian", "Thai", "Swedish", "Czech", "Greek", "Hungarian", "Romanian", "Finnish", "Danish", "Norwegian", "Slovenian", "Slovak", "Croatian", "Serbian", "Bulgarian"};
		static const int s_NumTranslateLang = std::size(s_apTranslateLangCodes);
		const auto FindTranslateLang = [&](const char *pCode) {
			for(int i = 0; i < s_NumTranslateLang; ++i)
				if(str_comp(pCode, s_apTranslateLangCodes[i]) == 0)
					return i;
			return 0;
		};

		CUIRect TranslateRow, TranslateLabel, TranslateDropDown;

		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(LineSize, &TranslateRow, &RightView);
		TranslateRow.VSplitMid(&TranslateLabel, &TranslateDropDown, MarginSmall);
		Ui()->DoLabel(&TranslateLabel, Localize("Translate incoming to"), ColorPickerLabelSize, TEXTALIGN_ML);
		static CUi::SDropDownState s_InTargetDropDownState;
		static CScrollRegion s_InTargetScrollRegion;
		s_InTargetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_InTargetScrollRegion;
		const int OldInTarget = FindTranslateLang(g_Config.m_ClChatTranslateInTarget);
		const int NewInTarget = Ui()->DoDropDown(&TranslateDropDown, OldInTarget, s_apTranslateLangNames, s_NumTranslateLang, s_InTargetDropDownState);
		if(NewInTarget != OldInTarget)
		{
			str_copy(g_Config.m_ClChatTranslateInTarget, s_apTranslateLangCodes[NewInTarget]);
			GameClient()->m_Chat.ClearTranslationCache();
		}

		{
			CUIRect IgnoreRow, IgnoreLabel, IgnoreEditBox;
			RightView.HSplitTop(MarginSmall, nullptr, &RightView);
			RightView.HSplitTop(LineSize, &IgnoreRow, &RightView);
			IgnoreRow.VSplitMid(&IgnoreLabel, &IgnoreEditBox, MarginSmall);
			Ui()->DoLabel(&IgnoreLabel, Localize("Ignored languages (e.g. \"en,de\")"), ColorPickerLabelSize, TEXTALIGN_ML);
			static CLineInput s_IgnoreInput(g_Config.m_ClChatTranslateIgnore, sizeof(g_Config.m_ClChatTranslateIgnore));
			if(Ui()->DoClearableEditBox(&s_IgnoreInput, &IgnoreEditBox, 14.0f))
				GameClient()->m_Chat.ClearTranslationCache();
		}

		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(LineSize, &TranslateRow, &RightView);
		TranslateRow.VSplitMid(&TranslateLabel, &TranslateDropDown, MarginSmall);
		Ui()->DoLabel(&TranslateLabel, Localize("Translate chat: your language"), ColorPickerLabelSize, TEXTALIGN_ML);
		static CUi::SDropDownState s_SourceLangDropDownState;
		static CScrollRegion s_SourceLangScrollRegion;
		s_SourceLangDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_SourceLangScrollRegion;
		const int OldSourceLang = FindTranslateLang(g_Config.m_ClChatTranslateOutSource);
		const int NewSourceLang = Ui()->DoDropDown(&TranslateDropDown, OldSourceLang, s_apTranslateLangNames, s_NumTranslateLang, s_SourceLangDropDownState);
		if(NewSourceLang != OldSourceLang)
			str_copy(g_Config.m_ClChatTranslateOutSource, s_apTranslateLangCodes[NewSourceLang]);

		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(LineSize, &TranslateRow, &RightView);
		TranslateRow.VSplitMid(&TranslateLabel, &TranslateDropDown, MarginSmall);
		Ui()->DoLabel(&TranslateLabel, Localize("Translate chat: send as"), ColorPickerLabelSize, TEXTALIGN_ML);
		static CUi::SDropDownState s_TargetLangDropDownState;
		static CScrollRegion s_TargetLangScrollRegion;
		s_TargetLangDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TargetLangScrollRegion;
		const int OldTargetLang = FindTranslateLang(g_Config.m_ClChatTranslateOutTarget);
		const int NewTargetLang = Ui()->DoDropDown(&TranslateDropDown, OldTargetLang, s_apTranslateLangNames, s_NumTranslateLang, s_TargetLangDropDownState);
		if(NewTargetLang != OldTargetLang)
			str_copy(g_Config.m_ClChatTranslateOutTarget, s_apTranslateLangCodes[NewTargetLang]);
	}
	else if(s_CurTab == MCLIENT_TAB_FROZEN)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		Ui()->DoLabel_AutoLineSize(Localize("Frozen weapon"), HeadlineFontSize, TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenWeapon, Localize("Show weapons of frozen tees"), &g_Config.m_ClMClientFrozenWeapon, &LeftView, LineSize);
		if(g_Config.m_ClMClientFrozenWeapon)
		{
			static CButtonContainer s_FrozenHammerColorResetId;
			DoLine_ColorPicker(&s_FrozenHammerColorResetId, 25.0f, 13.0f, 2.0f, &LeftView, Localize("Weapon color while trying to interact"), &g_Config.m_ClMClientFrozenHammerColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientFrozenHammerColor, false)), false, nullptr, false);
			LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientFrozenHammerColorAlpha, &g_Config.m_ClMClientFrozenHammerColorAlpha, &Button, Localize("Weapon color opacity"), 10, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenWeaponHammerOnly, Localize("Only recolor when holding the hammer"), &g_Config.m_ClMClientFrozenWeaponHammerOnly, &LeftView, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenWeaponNotSelf, Localize("Do not recolor your own tee and dummy"), &g_Config.m_ClMClientFrozenWeaponNotSelf, &LeftView, LineSize);
		}

		Ui()->DoLabel_AutoLineSize(Localize("Frozen teammates HUD"), HeadlineFontSize, TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenHud, Localize("Show frozen teammates on HUD"), &g_Config.m_ClMClientFrozenHud, &RightView, LineSize);
		if(g_Config.m_ClMClientFrozenHud)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenHudSkins, Localize("Use player skins for frozen tees"), &g_Config.m_ClMClientFrozenHudSkins, &RightView, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientFrozenHudTeamOnly, Localize("Only show frozen tees while in a team"), &g_Config.m_ClMClientFrozenHudTeamOnly, &RightView, LineSize);
			RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientFrozenHudTeeSize, &g_Config.m_ClMClientFrozenHudTeeSize, &Button, Localize("Frozen tee size"), 8, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
			RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientFrozenMaxRows, &g_Config.m_ClMClientFrozenMaxRows, &Button, Localize("Max frozen tee rows"), 1, 6, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
			RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
			Ui()->DoScrollbarOption(&g_Config.m_ClMClientFrozenNearDistance, &g_Config.m_ClMClientFrozenNearDistance, &Button, Localize("Nearby range when not in a team"), 100, 5000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
		}
	}
	else if(s_CurTab == MCLIENT_TAB_APPEARANCE)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		Ui()->DoLabel_AutoLineSize(Localize("Appearance"), HeadlineFontSize, TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		static CButtonContainer s_MClientColorResetId;
		DoLine_ColorPicker(&s_MClientColorResetId, 25.0f, 13.0f, 2.0f, &LeftView, Localize("Menu accent color"), &g_Config.m_ClMClientColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientColor, false)), false, nullptr, false);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClMClientMenuAnimation, Localize("Animated menu background"), &g_Config.m_ClMClientMenuAnimation, &LeftView, LineSize);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMClientMenuCursorSize, &g_Config.m_ClMClientMenuCursorSize, &Button, Localize("Menu cursor size"), 50, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMClientIngameCursorSize, &g_Config.m_ClMClientIngameCursorSize, &Button, Localize("Ingame cursor size"), 50, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

		Ui()->DoLabel_AutoLineSize(Localize("Accessibility"), HeadlineFontSize, TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMClientTextBrightness, &g_Config.m_ClMClientTextBrightness, &Button, Localize("UI text brightness"), 30, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

		static int s_ChatBackgroundDarkness;
		s_ChatBackgroundDarkness = round_to_int(ColorHSLA(g_Config.m_ClChatBackgroundColor, true).a * 100.0f);
		RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
		if(Ui()->DoScrollbarOption(&s_ChatBackgroundDarkness, &s_ChatBackgroundDarkness, &Button, Localize("Chat background darkness"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%"))
			g_Config.m_ClChatBackgroundColor = ColorHSLA(0.0f, 0.0f, 0.0f, s_ChatBackgroundDarkness / 100.0f).Pack(true);
	}
}
void CMenus::RenderSettingsTeeCompanion(CUIRect MainView)
{
	CUIRect Label, Button;

	// Enable toggle
	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClMClientPetTee, Localize("Show companion tee (pet)"), g_Config.m_ClMClientPetTee, &Button))
	{
		g_Config.m_ClMClientPetTee ^= 1;
	}
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	char *pSkinName = g_Config.m_ClMClientPetTeeSkin;
	const size_t SkinNameSize = sizeof(g_Config.m_ClMClientPetTeeSkin);

	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName[0] == '\0' ? "default" : pSkinName);
	if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
	{
		pOwnSkinContainer = nullptr;
	}

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
	OwnSkinInfo.m_Size = 50.0f;

	// Preview tee + skin name edit box
	CUIRect SkinArea, TeeRect, EditArea;
	MainView.HSplitTop(50.0f, &SkinArea, &MainView);
	SkinArea.VSplitLeft(65.0f, &TeeRect, &EditArea);
	EditArea.VSplitRight(20.0f, &EditArea, nullptr);
	EditArea.HSplitTop(15.0f, &Label, &EditArea);
	Ui()->DoLabel(&Label, Localize("Companion skin"), 14.0f, TEXTALIGN_ML);
	EditArea.HSplitTop(20.0f, &EditArea, nullptr);

	{
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(TeeRect.x + TeeRect.w / 2.0f, TeeRect.y + TeeRect.h / 2.0f + OffsetToMid.y);
		const vec2 DeltaPosition = Ui()->MousePos() - TeeRenderPos;
		const float Distance = length(DeltaPosition);
		const vec2 TeeDirection = Distance < 20.0f ? normalize(vec2(DeltaPosition.x, std::max(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < 20.0f ? EMOTE_HAPPY : EMOTE_NORMAL;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeeRenderPos);
	}

	static CLineInput s_SkinInput;
	s_SkinInput.SetBuffer(pSkinName, SkinNameSize);
	s_SkinInput.SetEmptyText("default");
	if(Ui()->DoClearableEditBox(&s_SkinInput, &EditArea, 14.0f))
	{
		m_SkinListScrollToSelected = true;
		SkinList.ForceRefresh();
	}

	MainView.HSplitTop(5.0f, nullptr, &MainView);

	// Bottom controls
	CUIRect QuickSearch, DirectoryButton, RefreshButton;
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &RefreshButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	DirectoryButton.VSplitRight(150.0f, nullptr, &DirectoryButton);

	// Skin selector, highlighting the current companion skin
	static CListBox s_ListBox;
	std::vector<CSkins::CSkinListEntry> &vSkinList = SkinList.Skins();
	int OldSelected = -1;
	s_ListBox.DoStart(50.0f, vSkinList.size(), 4, 2, OldSelected, &MainView);
	for(size_t i = 0; i < vSkinList.size(); ++i)
	{
		CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
		const CSkins::CSkinContainer *pSkinContainer = vSkinList[i].SkinContainer();

		if(str_comp_nocase(pSkinContainer->Name(), pSkinName) == 0)
		{
			OldSelected = i;
			if(m_SkinListScrollToSelected)
			{
				s_ListBox.ScrollToSelected();
				m_SkinListScrollToSelected = false;
			}
		}

		const CListboxItem Item = s_ListBox.DoNextItem(SkinListEntry.ListItemId(), OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
		{
			continue;
		}

		SkinListEntry.RequestLoad();
		const CSkin *pSkin = pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;

		Item.m_Rect.VSplitLeft(60.0f, &Button, &Label);
		{
			CTeeRenderInfo Info = OwnSkinInfo;
			Info.Apply(pSkin);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2.0f + OffsetToMid.y);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
		}
		Ui()->DoLabel(&Label, pSkinContainer->Name(), 12.0f, TEXTALIGN_ML);
	}
	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		str_copy(pSkinName, vSkinList[NewSelected].SkinContainer()->Name(), SkinNameSize);
		SkinList.ForceRefresh();
	}

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(DoButton_Menu(&s_SkinRefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}
void CMenus::RenderSettingsBindWheel(CUIRect MainView)
{
	CBindWheel &BindWheel = GameClient()->m_BindWheel;
	if((int)BindWheel.m_vBinds.size() != CBindWheel::TOTAL_BINDS)
		BindWheel.m_vBinds.resize(CBindWheel::TOTAL_BINDS);

	const int SliceCount = CBindWheel::NumSlots();
	const int PageCount = CBindWheel::NumPages();
	static int s_Selected = 0;
	static int s_Page = 0;
	s_Selected = std::clamp(s_Selected, 0, SliceCount - 1);
	s_Page = std::clamp(s_Page, 0, PageCount - 1);
	const auto &&SelectedBind = [&]() -> CBindWheel::CBind & {
		return BindWheel.m_vBinds[CBindWheel::BindIndex(s_Page, s_Selected)];
	};

	CUIRect Headline, Hint, Left, Right, Label, Field;

	const auto &&DoOptionRow = [&](int *pValue, const char *pText, int Min, int Max, const char *pSuffix) {
		CUIRect Row, OptionLabel, ScrollBar;
		Left.HSplitTop(20.0f, &Row, &Left);
		Left.HSplitTop(6.0f, nullptr, &Left);
		Row.VSplitLeft(Row.w * 0.55f, &OptionLabel, &ScrollBar);
		ScrollBar.VSplitLeft(10.0f, nullptr, &ScrollBar);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %d%s", pText, *pValue, pSuffix);
		SLabelProperties Props;
		Props.m_EnableWidthCheck = false;
		Ui()->DoLabel(&OptionLabel, aBuf, 12.8f, TEXTALIGN_ML, Props);

		const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(*pValue, Min, Max);
		const int NewValue = CUi::ms_LinearScrollbarScale.ToAbsolute(Ui()->DoScrollbarH(pValue, &ScrollBar, Relative), Min, Max);
		if(NewValue == *pValue)
			return false;
		*pValue = NewValue;
		return true;
	};

	MainView.HSplitTop(30.0f, &Headline, &MainView);
	Ui()->DoLabel(&Headline, Localize("Bind Wheel"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(18.0f, &Hint, &MainView);
	{
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel(&Hint, Localize("Bind a key to \"Bind wheel\" under Controls, then hold it in-game and release over a slice. Scroll to switch pages."), 11.0f, TEXTALIGN_ML, Props);
	}
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	MainView.VSplitLeft(330.0f, &Left, &Right);
	Right.VSplitLeft(20.0f, nullptr, &Right);

	// number of slices
	DoOptionRow(&g_Config.m_ClMClientBindWheelSlots, Localize("Number of slices"), CBindWheel::MIN_BINDS, CBindWheel::MAX_BINDS, "");

	// number of pages
	DoOptionRow(&g_Config.m_ClMClientBindWheelPages, Localize("Number of pages"), 1, CBindWheel::MAX_PAGES, "");

	// size of the page number in the middle
	DoOptionRow(&g_Config.m_ClMClientBindWheelPageNumberSize, Localize("Page number size"), 0, 60, "");

	// opacity of the page number
	DoOptionRow(&g_Config.m_ClMClientBindWheelPageNumberAlpha, Localize("Page number opacity"), 0, 100, "%");

	// invert the scroll direction
	CUIRect InvertRow;
	Left.HSplitTop(20.0f, &InvertRow, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClMClientBindWheelScrollInvert, Localize("Invert page scroll direction"), g_Config.m_ClMClientBindWheelScrollInvert, &InvertRow))
		g_Config.m_ClMClientBindWheelScrollInvert ^= 1;
	Left.HSplitTop(6.0f, nullptr, &Left);

	// background opacity
	DoOptionRow(&g_Config.m_ClMClientBindWheelAlpha, Localize("Background opacity"), 0, 100, "%");

	// circle size
	DoOptionRow(&g_Config.m_ClMClientBindWheelSize, Localize("Circle size"), 50, 120, "%");

	// box width
	DoOptionRow(&g_Config.m_ClMClientBindWheelBoxWidth, Localize("Box width"), 30, 160, "");

	// box height
	DoOptionRow(&g_Config.m_ClMClientBindWheelBoxHeight, Localize("Box height"), 24, 64, "");

	// box opacity
	DoOptionRow(&g_Config.m_ClMClientBindWheelBoxAlpha, Localize("Box opacity"), 0, 100, "%");

	// color of the selected box
	static CButtonContainer s_BoxColorResetId;
	DoLine_ColorPicker(&s_BoxColorResetId, 20.0f, 12.0f, 6.0f, &Left, Localize("Selected box color"), &g_Config.m_ClMClientBindWheelBoxColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientBindWheelBoxColor, false)), false, nullptr, false);
	Left.HSplitTop(24.0f, nullptr, &Left);

	static int s_DragSource = -1;
	static int s_DragPage = 0;
	static vec2 s_DragStart = vec2(0.0f, 0.0f);
	const bool Dragging = s_DragSource >= 0 && length(Ui()->MousePos() - s_DragStart) > 5.0f;

	const float TabHeight = 20.0f;
	if(PageCount > 1)
	{
		CUIRect Tabs = Right;
		Tabs.h = TabHeight;
		static CButtonContainer s_aPageButtons[CBindWheel::MAX_PAGES];
		const float Spacing = 3.0f;
		const float TabWidth = (Tabs.w - (PageCount - 1) * Spacing) / PageCount;
		for(int Page = 0; Page < PageCount; Page++)
		{
			CUIRect Tab;
			Tabs.VSplitLeft(TabWidth, &Tab, &Tabs);
			Tabs.VSplitLeft(Spacing, nullptr, &Tabs);
			char aLabel[8];
			str_format(aLabel, sizeof(aLabel), "%d", Page + 1);
			if(DoButton_MenuTab(&s_aPageButtons[Page], aLabel, s_Page == Page, &Tab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 5.0f))
				s_Page = Page;
			else if(Dragging && Ui()->MouseHovered(&Tab))
				s_Page = Page;
		}
		if(Ui()->MouseHovered(&Right))
		{
			const int Forward = g_Config.m_ClMClientBindWheelScrollInvert ? PageCount - 1 : 1;
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
				s_Page = (s_Page + Forward) % PageCount;
			else if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				s_Page = (s_Page + PageCount - Forward) % PageCount;
		}
	}

	const ColorRGBA HighlightColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMClientBindWheelBoxColor, false));

	// wheel preview
	const float GameRadius = 215.0f * (g_Config.m_ClMClientBindWheelSize / 100.0f);
	const float BoxWidth = (float)g_Config.m_ClMClientBindWheelBoxWidth;
	const float BoxHeight = (float)g_Config.m_ClMClientBindWheelBoxHeight;
	const float WheelExtent = GameRadius + 20.0f + std::max(BoxWidth, BoxHeight) / 2.0f;
	const float FitExtent = std::min(Right.w, Right.h - 2.0f * (TabHeight + 6.0f)) / 2.0f;
	const float Scale = std::min(Right.h / Ui()->Screen()->h, FitExtent / WheelExtent);
	const float Radius = GameRadius * Scale;
	const float SlotWidth = BoxWidth * Scale;
	const float SlotHeight = BoxHeight * Scale;
	const vec2 Center = vec2(Right.x + Right.w / 2.0f, Right.y + Right.h / 2.0f);

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.04f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius + 20.0f * Scale, 64);
	Graphics()->QuadsEnd();

	const float PageNumberSize = g_Config.m_ClMClientBindWheelPageNumberSize * Scale;
	if(PageNumberSize > 0.0f)
	{
		char aPage[8];
		str_format(aPage, sizeof(aPage), "%d", s_Page + 1);
		CUIRect PageLabel = {Center.x - PageNumberSize * 1.5f, Center.y - PageNumberSize * 0.65f, PageNumberSize * 3.0f, PageNumberSize * 1.3f};
		const float PageNumberAlpha = g_Config.m_ClMClientBindWheelPageNumberAlpha / 100.0f;
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.35f * PageNumberAlpha);
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, PageNumberAlpha));
		Ui()->DoLabel(&PageLabel, aPage, PageNumberSize, TEXTALIGN_MC, Props);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	}

	// draws the icon
	const auto &&DrawSlotContents = [&](const CUIRect &Box, const CBindWheel::CBind &Bind) {
		const bool HasEmoji = Bind.m_aEmoji[0] != '\0';
		const char *pName;
		if(Bind.m_aName[0] != '\0')
			pName = Bind.m_aName;
		else if(HasEmoji)
			pName = "";
		else if(Bind.m_aCommand[0] != '\0')
			pName = Bind.m_aCommand;
		else
			pName = "-";
		CUIRect Text = Box;
		Text.VMargin(6.0f * Scale, &Text);
		const bool HasText = pName[0] != '\0' && (!HasEmoji || Text.w >= 44.0f * Scale);
		if(HasEmoji)
		{
			CUIRect IconRect = Text;
			if(HasText)
			{
				IconRect.w = 16.0f * Scale;
				Text.x += 18.0f * Scale;
				Text.w -= 18.0f * Scale;
			}
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			Ui()->DoLabel(&IconRect, Bind.m_aEmoji, (HasText ? 12.0f : 16.0f) * Scale, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		if(HasText)
		{
			SLabelProperties Props;
			Props.m_MaxWidth = Text.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Text, pName, 12.0f * Scale, TEXTALIGN_MC, Props);
		}
	};

	int HoverIndex = -1;
	for(int i = 0; i < SliceCount; i++)
	{
		float Angle = 2 * pi * i / (float)SliceCount;
		if(Angle > pi)
			Angle -= 2 * pi;
		const vec2 Pos = Center + direction(Angle) * Radius;
		CUIRect Slot = {Pos.x - SlotWidth / 2.0f, Pos.y - SlotHeight / 2.0f, SlotWidth, SlotHeight};
		const bool Hovered = Ui()->MouseHovered(&Slot);
		if(Hovered)
			HoverIndex = i;
		const bool Sel = s_Selected == i;
		ColorRGBA SlotColor = Sel ? HighlightColor.WithAlpha(1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.16f : 0.08f);
		if(Dragging && s_DragSource == i && s_DragPage == s_Page)
			SlotColor = SlotColor.WithMultipliedAlpha(0.35f);
		Slot.Draw(SlotColor, IGraphics::CORNER_ALL, 8.0f * Scale);

		DrawSlotContents(Slot, BindWheel.m_vBinds[CBindWheel::BindIndex(s_Page, i)]);
	}

	// start a drag
	if(s_DragSource < 0 && HoverIndex >= 0 && Ui()->MouseButtonClicked(0))
	{
		s_DragSource = HoverIndex;
		s_DragPage = s_Page;
		s_DragStart = Ui()->MousePos();
		s_Selected = HoverIndex;
	}
	// finish on release
	if(s_DragSource >= 0 && !Ui()->MouseButton(0))
	{
		if(Dragging && HoverIndex >= 0)
		{
			const int Source = CBindWheel::BindIndex(s_DragPage, s_DragSource);
			const int Target = CBindWheel::BindIndex(s_Page, HoverIndex);
			if(Source != Target)
			{
				std::swap(BindWheel.m_vBinds[Source], BindWheel.m_vBinds[Target]);
				s_Selected = HoverIndex;
				SaveMClient();
			}
		}
		s_DragSource = -1;
	}

	if(Dragging && s_DragSource >= 0)
	{
		const vec2 M = Ui()->MousePos();
		CUIRect Ghost = {M.x - SlotWidth / 2.0f, M.y - SlotHeight / 2.0f, SlotWidth, SlotHeight};
		Ghost.Draw(HighlightColor.WithAlpha(0.85f), IGraphics::CORNER_ALL, 8.0f * Scale);
		DrawSlotContents(Ghost, BindWheel.m_vBinds[CBindWheel::BindIndex(s_DragPage, s_DragSource)]);
	}

	// editor for the selected slice
	char aTitle[64];
	if(PageCount > 1)
		str_format(aTitle, sizeof(aTitle), "%s %d \xe2\x80\x93 %s %d", Localize("Page"), s_Page + 1, Localize("Slice"), s_Selected + 1);
	else
		str_format(aTitle, sizeof(aTitle), "%s %d", Localize("Slice"), s_Selected + 1);
	Left.HSplitTop(24.0f, &Label, &Left);
	Ui()->DoLabel(&Label, aTitle, 16.0f, TEXTALIGN_ML);
	Left.HSplitTop(12.0f, nullptr, &Left);

	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Label"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(24.0f, &Field, &Left);

	CUIRect EmojiBox;
	Field.VSplitLeft(24.0f, &EmojiBox, &Field);
	Field.VSplitLeft(6.0f, nullptr, &Field);
	static CButtonContainer s_EmojiButton;
	const char *pCurEmoji = SelectedBind().m_aEmoji;
	if(Ui()->DoButton_FontIcon(&s_EmojiButton, pCurEmoji[0] != '\0' ? pCurEmoji : FontIcon::PLUS, 0, &EmojiBox, BUTTONFLAG_LEFT))
	{
		static SPopupBindWheelIconContext s_PopupIconContext;
		s_PopupIconContext.m_pMenus = this;
		s_PopupIconContext.m_BindIndex = CBindWheel::BindIndex(s_Page, s_Selected);
		Ui()->DoPopupMenu(&s_PopupIconContext, EmojiBox.x, EmojiBox.y + EmojiBox.h, 232.0f, 210.0f, &s_PopupIconContext, PopupBindWheelIcon);
	}

	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(SelectedBind().m_aName, sizeof(SelectedBind().m_aName));
	s_NameInput.SetEmptyText(Localize("e.g. Kill"));
	if(Ui()->DoClearableEditBox(&s_NameInput, &Field, 12.0f))
		SaveMClient();

	Left.HSplitTop(12.0f, nullptr, &Left);
	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Console command (as typed in the F1 console)"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(24.0f, &Field, &Left);
	static CLineInput s_CommandInput;
	s_CommandInput.SetBuffer(SelectedBind().m_aCommand, sizeof(SelectedBind().m_aCommand));
	s_CommandInput.SetEmptyText(Localize("e.g. kill"));
	if(Ui()->DoClearableEditBox(&s_CommandInput, &Field, 12.0f))
		SaveMClient();
}
CUi::EPopupMenuFunctionResult CMenus::PopupBindWheelIcon(void *pContext, CUIRect View, bool Active)
{
	SPopupBindWheelIconContext *pPopupContext = static_cast<SPopupBindWheelIconContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	CBindWheel &BindWheel = pMenus->GameClient()->m_BindWheel;
	if(pPopupContext->m_BindIndex < 0 || pPopupContext->m_BindIndex >= (int)BindWheel.m_vBinds.size())
		return CUi::POPUP_CLOSE_CURRENT;
	CBindWheel::CBind &Bind = BindWheel.m_vBinds[pPopupContext->m_BindIndex];

	static const char *const s_apIcons[] = {
		FontIcon::HEART, FontIcon::HEART_CRACK, FontIcon::STAR, FontIcon::FLAG_CHECKERED,
		FontIcon::XMARK, FontIcon::BAN, FontIcon::BOOKMARK, FontIcon::CAMERA,
		FontIcon::COMMENT, FontIcon::COMMENT_SLASH, FontIcon::EYE, FontIcon::EYE_SLASH,
		FontIcon::GEAR, FontIcon::HOUSE, FontIcon::KEY, FontIcon::LOCK,
		FontIcon::MAGNIFYING_GLASS, FontIcon::MAP, FontIcon::MUSIC, FontIcon::PAUSE,
		FontIcon::PLAY, FontIcon::STOP, FontIcon::POWER_OFF, FontIcon::QUESTION,
		FontIcon::INFO, FontIcon::TRIANGLE_EXCLAMATION, FontIcon::TRASH, FontIcon::USER,
		FontIcon::VIDEO, FontIcon::TERMINAL, FontIcon::EARTH_AMERICAS, FontIcon::CIRCLE};
	const int NumIcons = (int)(sizeof(s_apIcons) / sizeof(s_apIcons[0]));

	View.Margin(6.0f, &View);

	CUIRect ClearButton;
	View.HSplitTop(20.0f, &ClearButton, &View);
	static CButtonContainer s_ClearButton;
	if(pMenus->DoButton_Menu(&s_ClearButton, Localize("No icon"), 0, &ClearButton))
	{
		Bind.m_aEmoji[0] = '\0';
		pMenus->SaveMClient();
		return CUi::POPUP_CLOSE_CURRENT;
	}
	View.HSplitTop(6.0f, nullptr, &View);

	const int Columns = 8;
	const float Spacing = 2.0f;
	const float CellSize = (View.w - (Columns - 1) * Spacing) / Columns;

	static CButtonContainer s_aIconButtons[64];
	CUIRect Row;
	for(int i = 0; i < NumIcons; i++)
	{
		if(i % Columns == 0)
			View.HSplitTop(CellSize, &Row, &View);
		CUIRect Cell;
		Row.VSplitLeft(CellSize, &Cell, &Row);
		Row.VSplitLeft(Spacing, nullptr, &Row);
		const bool Selected = str_comp(Bind.m_aEmoji, s_apIcons[i]) == 0;
		if(pMenus->Ui()->DoButton_FontIcon(&s_aIconButtons[i], s_apIcons[i], Selected, &Cell, BUTTONFLAG_LEFT))
		{
			str_copy(Bind.m_aEmoji, s_apIcons[i]);
			pMenus->SaveMClient();
			return CUi::POPUP_CLOSE_CURRENT;
		}
		if(i % Columns == Columns - 1)
			View.HSplitTop(Spacing, nullptr, &View);
	}

	return CUi::POPUP_KEEP_OPEN;
}
void CMenus::RenderSettingsWidgetBar(CUIRect MainView)
{
	CUIRect Headline, Hint, Left, Right, Row, PreviewArea, PreviewCaption, PreviewBar;

	MainView.HSplitTop(30.0f, &Headline, &MainView);
	Ui()->DoLabel(&Headline, Localize("Widget Bar"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(18.0f, &Hint, &MainView);
	{
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel(&Hint, Localize("A customizable bar at the bottom of the screen. Choose which widgets to show and where they appear."), 11.0f, TEXTALIGN_ML, Props);
	}
	MainView.HSplitTop(12.0f, nullptr, &MainView);

	MainView.HSplitBottom(46.0f, &MainView, &PreviewArea);
	PreviewArea.HSplitTop(14.0f, &PreviewCaption, &PreviewArea);
	{
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
		Ui()->DoLabel(&PreviewCaption, Localize("Preview"), 11.0f, TEXTALIGN_ML, Props);
	}
	PreviewArea.HSplitTop((float)g_Config.m_ClMClientInfoBarHeight, &PreviewBar, nullptr);
	PreviewBar.Draw(ColorRGBA(0.32f, 0.34f, 0.40f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
	GameClient()->m_WidgetBar.RenderBar(PreviewBar);

	MainView.VSplitMid(&Left, &Right, 20.0f);

	// general
	Left.HSplitTop(20.0f, &Row, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClMClientInfoBar, Localize("Enable widget bar"), g_Config.m_ClMClientInfoBar, &Row))
		g_Config.m_ClMClientInfoBar ^= 1;
	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(20.0f, &Row, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_ClMClientInfoBarHeight, &g_Config.m_ClMClientInfoBarHeight, &Row, Localize("Bar height"), 12, 28);
	Left.HSplitTop(8.0f, nullptr, &Left);
	Left.HSplitTop(20.0f, &Row, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_ClMClientInfoBarAlpha, &g_Config.m_ClMClientInfoBarAlpha, &Row, Localize("Background opacity"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Left.HSplitTop(8.0f, nullptr, &Left);
	static CButtonContainer s_BgColor, s_LabelColor, s_ValueColor;
	DoLine_ColorPicker(&s_BgColor, 25.0f, 13.0f, 2.0f, &Left, Localize("Background color"), &g_Config.m_ClMClientInfoBarBgColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientInfoBarBgColor, false)), false, nullptr, false);
	DoLine_ColorPicker(&s_LabelColor, 25.0f, 13.0f, 2.0f, &Left, Localize("Label color"), &g_Config.m_ClMClientInfoBarLabelColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientInfoBarLabelColor, false)), false, nullptr, false);
	DoLine_ColorPicker(&s_ValueColor, 25.0f, 13.0f, 2.0f, &Left, Localize("Value color"), &g_Config.m_ClMClientInfoBarValueColor, color_cast<ColorRGBA>(ColorHSLA((unsigned)DefaultConfig::ClMClientInfoBarValueColor, false)), false, nullptr, false);

	// widgets
	const std::vector<const char *> vLabels = {Localize("Off"), Localize("Left"), Localize("Center"), Localize("Right")};
	const std::vector<int> vValues = {0, 1, 2, 3};
	static std::vector<CButtonContainer> s_vClock(4), s_vFps(4), s_vPing(4), s_vPred(4), s_vPos(4), s_vAngle(4), s_vSpeed(4);
	DoLine_RadioMenu(Right, Localize("Clock"), s_vClock, vLabels, vValues, g_Config.m_ClMClientInfoBarClock);
	DoLine_RadioMenu(Right, Localize("FPS"), s_vFps, vLabels, vValues, g_Config.m_ClMClientInfoBarFps);
	DoLine_RadioMenu(Right, Localize("Ping"), s_vPing, vLabels, vValues, g_Config.m_ClMClientInfoBarPing);
	DoLine_RadioMenu(Right, Localize("Prediction"), s_vPred, vLabels, vValues, g_Config.m_ClMClientInfoBarPred);
	DoLine_RadioMenu(Right, Localize("Position"), s_vPos, vLabels, vValues, g_Config.m_ClMClientInfoBarPos);
	DoLine_RadioMenu(Right, Localize("Angle"), s_vAngle, vLabels, vValues, g_Config.m_ClMClientInfoBarAngle);
	DoLine_RadioMenu(Right, Localize("Speed"), s_vSpeed, vLabels, vValues, g_Config.m_ClMClientInfoBarSpeed);
}
void CMenus::RenderSettingsProfiles(CUIRect MainView)
{
	if(!m_ProfilesLoaded)
		LoadProfiles();

	CUIRect Headline, TopBar, Divider;

	// renders a tee
	const auto &&RenderProfileTee = [this](const CUIRect &Rect, const char *pSkin, int UseCustomColor, int ColorBody, int ColorFeet) {
		if(pSkin[0] == '\0')
			return;
		const CTeeRenderInfo Info = GetTeeRenderInfo(vec2(Rect.w, Rect.h), pSkin, UseCustomColor, ColorBody, ColorFeet);
		const CAnimState *pIdle = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &Info, OffsetToMid);
		const vec2 Pos = vec2(Rect.x + Rect.w / 2.0f, Rect.y + Rect.h * 0.55f + OffsetToMid.y);
		RenderTools()->RenderTee(pIdle, &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Pos);
	};

	const auto &&MenuButton = [this](CUIRect Rect, const char *pText, float Size, bool Hovered) {
		Rect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.18f : 0.10f), IGraphics::CORNER_ALL, 5.0f);
		Ui()->DoLabel(&Rect, pText, Size, TEXTALIGN_MC);
	};

	// headline
	MainView.HSplitTop(30.0f, &Headline, &MainView);
	Ui()->DoLabel(&Headline, Localize("Profiles"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	// current player + "save current profile" button
	MainView.HSplitTop(34.0f, &TopBar, &MainView);
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	CUIRect CurTee, CurInfo, CurLabel, SaveButton, CurFlag;
	TopBar.VSplitRight(170.0f, &TopBar, &SaveButton);
	TopBar.VSplitRight(10.0f, &TopBar, nullptr);
	TopBar.VSplitLeft(TopBar.h, &CurTee, &CurInfo);
	CurInfo.VSplitLeft(8.0f, nullptr, &CurInfo);

	RenderProfileTee(CurTee, g_Config.m_ClPlayerSkin, g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
	{
		char aCurrent[64];
		if(g_Config.m_PlayerClan[0] != '\0')
			str_format(aCurrent, sizeof(aCurrent), "%s [%s]", g_Config.m_PlayerName, g_Config.m_PlayerClan);
		else
			str_copy(aCurrent, g_Config.m_PlayerName);
		const float TextW = std::min(TextRender()->TextWidth(14.0f, aCurrent), CurInfo.w - 30.0f);
		CurInfo.VSplitLeft(TextW + 6.0f, &CurLabel, &CurInfo);
		Ui()->DoLabel(&CurLabel, aCurrent, 14.0f, TEXTALIGN_ML, {.m_MaxWidth = CurLabel.w});
	}
	CurInfo.VSplitLeft(24.0f, &CurFlag, &CurInfo);
	CurFlag.HMargin((CurFlag.h - 14.0f) / 2.0f, &CurFlag);
	if(g_Config.m_PlayerCountry >= 0)
		GameClient()->m_CountryFlags.Render(g_Config.m_PlayerCountry, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), CurFlag.x, CurFlag.y, CurFlag.w, CurFlag.h);

	SaveButton.HMargin((SaveButton.h - 24.0f) / 2.0f, &SaveButton);
	static CButtonContainer s_SaveButton;
	MenuButton(SaveButton, Localize("Save current profile"), 14.0f, Ui()->HotItem() == &s_SaveButton);
	if(Ui()->DoButtonLogic(&s_SaveButton, 0, &SaveButton, BUTTONFLAG_LEFT))
	{
		CProfile Profile;
		str_copy(Profile.m_aName, g_Config.m_PlayerName);
		str_copy(Profile.m_aClan, g_Config.m_PlayerClan);
		Profile.m_Country = g_Config.m_PlayerCountry;
		str_copy(Profile.m_aSkin, g_Config.m_ClPlayerSkin);
		Profile.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor;
		Profile.m_ColorBody = g_Config.m_ClPlayerColorBody;
		Profile.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
		m_vProfiles.push_back(Profile);
		SaveMClient();
	}

	MainView.HSplitTop(1.0f, &Divider, &MainView);
	Divider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_NONE, 0.0f);
	MainView.HSplitTop(6.0f, nullptr, &MainView);

	if(m_vProfiles.empty())
	{
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
		CUIRect Hint;
		MainView.HSplitTop(24.0f, &Hint, nullptr);
		Ui()->DoLabel(&Hint, Localize("No profiles saved yet. Click \"Save current profile\" to add one."), 12.0f, TEXTALIGN_ML, Props);
		return;
	}

	static int s_Selected = -1;
	if(s_Selected >= (int)m_vProfiles.size())
		s_Selected = -1;

	static CListBox s_ListBox;
	s_ListBox.SetRowColors(
		AccentColor().WithAlpha(0.14f),
		AccentColor().WithAlpha(0.10f),
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f));
	s_ListBox.DoStart(30.0f, m_vProfiles.size(), 1, 3, s_Selected, &MainView, false);

	int LoadIndex = -1;
	int DeleteIndex = -1;
	for(size_t i = 0; i < m_vProfiles.size(); ++i)
	{
		const CProfile &Profile = m_vProfiles[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&m_vProfiles[i], (int)i == s_Selected);
		if(!Item.m_Visible)
			continue;

		CUIRect Row = Item.m_Rect, TeeRect, FlagRect, LoadButton, DeleteButton, NameCol, ClanCol, CmdBox;
		Row.VSplitLeft(6.0f, nullptr, &Row);
		Row.VSplitLeft(Row.h, &TeeRect, &Row);
		Row.VSplitLeft(6.0f, nullptr, &Row);
		Row.VSplitRight(26.0f, &Row, &DeleteButton);
		Row.VSplitRight(4.0f, &Row, nullptr);
		Row.VSplitRight(56.0f, &Row, &LoadButton);
		Row.VSplitRight(10.0f, &Row, nullptr);

		const float NameW = std::min(TextRender()->TextWidth(12.0f, Profile.m_aName[0] != '\0' ? Profile.m_aName : "(unnamed)"), Row.w * 0.5f);
		Row.VSplitLeft(NameW + 12.0f, &NameCol, &Row);
		const float ClanW = Profile.m_aClan[0] != '\0' ? std::min(TextRender()->TextWidth(12.0f, Profile.m_aClan), Row.w - 40.0f) : -8.0f;
		Row.VSplitLeft(ClanW + 8.0f, &ClanCol, &Row);
		Row.VSplitLeft(24.0f, &FlagRect, &Row);
		char aLoadCmd[64];
		str_format(aLoadCmd, sizeof(aLoadCmd), "load_profile %d", (int)i + 1);
		Row.VSplitLeft(10.0f, nullptr, &Row);
		Row.VSplitLeft(TextRender()->TextWidth(10.0f, aLoadCmd) + 18.0f, &CmdBox, &Row);

		// tee preview
		RenderProfileTee(TeeRect, Profile.m_aSkin, Profile.m_UseCustomColor, Profile.m_ColorBody, Profile.m_ColorFeet);

		// name + clan
		SLabelProperties NameProps;
		NameProps.m_MaxWidth = NameCol.w;
		NameProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&NameCol, Profile.m_aName[0] != '\0' ? Profile.m_aName : "(unnamed)", 12.0f, TEXTALIGN_ML, NameProps);
		if(Profile.m_aClan[0] != '\0')
		{
			SLabelProperties ClanProps;
			ClanProps.m_MaxWidth = ClanCol.w;
			ClanProps.m_EllipsisAtEnd = true;
			ClanProps.SetColor(ColorRGBA(0.60f, 0.60f, 0.60f, 1.0f));
			Ui()->DoLabel(&ClanCol, Profile.m_aClan, 12.0f, TEXTALIGN_ML, ClanProps);
		}

		// flag
		FlagRect.HMargin((FlagRect.h - 14.0f) / 2.0f, &FlagRect);
		if(Profile.m_Country >= 0)
			GameClient()->m_CountryFlags.Render(Profile.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		// copyable console command
		CmdBox.HMargin((CmdBox.h - 18.0f) / 2.0f, &CmdBox);
		const bool CmdHovered = Ui()->HotItem() == &Profile.m_ColorBody;
		CmdBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, CmdHovered ? 0.12f : 0.06f), IGraphics::CORNER_ALL, 3.0f);
		SLabelProperties CmdProps;
		CmdProps.SetColor(CmdHovered ? ColorRGBA(0.90f, 0.90f, 0.90f, 1.0f) : ColorRGBA(0.60f, 0.60f, 0.60f, 1.0f));
		Ui()->DoLabel(&CmdBox, aLoadCmd, 10.0f, TEXTALIGN_MC, CmdProps);
		if(Ui()->DoButtonLogic(&Profile.m_ColorBody, 0, &CmdBox, BUTTONFLAG_LEFT))
			Input()->SetClipboardText(aLoadCmd);
		GameClient()->m_Tooltips.DoToolTip(&Profile.m_ColorBody, &CmdBox, Localize("Click to copy this console command (F1)"));

		// load button
		LoadButton.HMargin((LoadButton.h - 18.0f) / 2.0f, &LoadButton);
		MenuButton(LoadButton, Localize("Load"), 11.0f, Ui()->HotItem() == &Profile.m_aSkin);
		if(Ui()->DoButtonLogic(&Profile.m_aSkin, 0, &LoadButton, BUTTONFLAG_LEFT))
			LoadIndex = (int)i;

		// delete button
		const bool DeleteHovered = Ui()->HotItem() == &Profile.m_aClan;
		SLabelProperties TrashProps;
		TrashProps.SetColor(DeleteHovered ? ColorRGBA(0.95f, 0.45f, 0.45f, 1.0f) : ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&DeleteButton, FontIcon::TRASH, 12.0f, TEXTALIGN_MC, TrashProps);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(Ui()->DoButtonLogic(&Profile.m_aClan, 0, &DeleteButton, BUTTONFLAG_LEFT))
			DeleteIndex = (int)i;
	}
	s_Selected = s_ListBox.DoEnd();

	if(LoadIndex >= 0)
		ApplyProfile(m_vProfiles[LoadIndex]);

	if(DeleteIndex >= 0)
	{
		m_vProfiles.erase(m_vProfiles.begin() + DeleteIndex);
		SaveMClient();
		s_Selected = -1;
	}
}
static const char *const MCLIENT_FILE = "mclient.json";

void CMenus::LoadProfiles()
{
	m_ProfilesLoaded = true;
	m_vProfiles.clear();

	void *pFileData;
	unsigned FileSize;
	if(!Storage()->ReadFile(MCLIENT_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pData = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileSize, aError);
	free(pFileData);
	if(pData == nullptr)
		return;

	const json_value &Profiles = (*pData)["profiles"];
	if(Profiles.type == json_array)
	{
		for(unsigned i = 0; i < Profiles.u.array.length; ++i)
		{
			const json_value &Entry = Profiles[i];
			if(Entry.type != json_object)
				continue;
			CProfile Profile;
			const json_value &Name = Entry["name"];
			const json_value &Clan = Entry["clan"];
			const json_value &Country = Entry["country"];
			const json_value &Skin = Entry["skin"];
			const json_value &UseCustomColor = Entry["use_custom_color"];
			const json_value &ColorBody = Entry["color_body"];
			const json_value &ColorFeet = Entry["color_feet"];
			if(Name.type == json_string)
				str_copy(Profile.m_aName, Name);
			if(Clan.type == json_string)
				str_copy(Profile.m_aClan, Clan);
			if(Country.type == json_integer)
				Profile.m_Country = (int)Country.u.integer;
			if(Skin.type == json_string)
				str_copy(Profile.m_aSkin, Skin);
			if(UseCustomColor.type == json_integer)
				Profile.m_UseCustomColor = (int)UseCustomColor.u.integer;
			if(ColorBody.type == json_integer)
				Profile.m_ColorBody = (int)ColorBody.u.integer;
			if(ColorFeet.type == json_integer)
				Profile.m_ColorFeet = (int)ColorFeet.u.integer;
			m_vProfiles.push_back(Profile);
		}
	}
	json_value_free(pData);
}
void CMenus::SaveMClient()
{
	if(!m_ProfilesLoaded)
		LoadProfiles();
	if(!m_SavedMapsLoaded)
		LoadSavedMaps();

	IOHANDLE File = Storage()->OpenFile(MCLIENT_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	CJsonFileWriter Writer(File);
	Writer.BeginObject();

	Writer.WriteAttribute("profiles");
	Writer.BeginArray();
	for(const CProfile &Profile : m_vProfiles)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("name");
		Writer.WriteStrValue(Profile.m_aName);
		Writer.WriteAttribute("clan");
		Writer.WriteStrValue(Profile.m_aClan);
		Writer.WriteAttribute("country");
		Writer.WriteIntValue(Profile.m_Country);
		Writer.WriteAttribute("skin");
		Writer.WriteStrValue(Profile.m_aSkin);
		Writer.WriteAttribute("use_custom_color");
		Writer.WriteIntValue(Profile.m_UseCustomColor);
		Writer.WriteAttribute("color_body");
		Writer.WriteIntValue(Profile.m_ColorBody);
		Writer.WriteAttribute("color_feet");
		Writer.WriteIntValue(Profile.m_ColorFeet);
		Writer.EndObject();
	}
	Writer.EndArray();

	const std::vector<CBindWheel::CBind> &vBinds = GameClient()->m_BindWheel.m_vBinds;
	const auto &&WriteBind = [&](const CBindWheel::CBind &Bind) {
		Writer.BeginObject();
		Writer.WriteAttribute("name");
		Writer.WriteStrValue(Bind.m_aName);
		Writer.WriteAttribute("command");
		Writer.WriteStrValue(Bind.m_aCommand);
		Writer.WriteAttribute("emoji");
		Writer.WriteStrValue(Bind.m_aEmoji);
		Writer.EndObject();
	};

	Writer.WriteAttribute("bindwheel");
	Writer.BeginArray();
	for(int i = 0; i < CBindWheel::MAX_BINDS; i++)
		WriteBind(vBinds[i]);
	Writer.EndArray();

	int NumPagesWritten = CBindWheel::NumPages();
	for(int Page = CBindWheel::MAX_PAGES - 1; Page >= NumPagesWritten; Page--)
	{
		bool Used = false;
		for(int i = 0; i < CBindWheel::MAX_BINDS && !Used; i++)
		{
			const CBindWheel::CBind &Bind = vBinds[CBindWheel::BindIndex(Page, i)];
			Used = Bind.m_aName[0] != '\0' || Bind.m_aCommand[0] != '\0' || Bind.m_aEmoji[0] != '\0';
		}
		if(Used)
		{
			NumPagesWritten = Page + 1;
			break;
		}
	}

	Writer.WriteAttribute("bindwheel_pages");
	Writer.BeginArray();
	for(int Page = 0; Page < NumPagesWritten; Page++)
	{
		Writer.BeginArray();
		for(int i = 0; i < CBindWheel::MAX_BINDS; i++)
			WriteBind(vBinds[CBindWheel::BindIndex(Page, i)]);
		Writer.EndArray();
	}
	Writer.EndArray();

	Writer.WriteAttribute("savedmaps");
	Writer.BeginArray();
	for(const CSavedMap &Saved : m_vSavedMaps)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("map");
		Writer.WriteStrValue(Saved.m_aMap);
		Writer.WriteAttribute("note");
		Writer.WriteStrValue(Saved.m_aNote);
		Writer.EndObject();
	}
	Writer.EndArray();

	Writer.EndObject();
}
void CMenus::ApplyProfile(const CProfile &Profile)
{
	str_copy(g_Config.m_PlayerName, Profile.m_aName);
	str_copy(g_Config.m_PlayerClan, Profile.m_aClan);
	if(Profile.m_Country >= 0)
		g_Config.m_PlayerCountry = Profile.m_Country;
	str_copy(g_Config.m_ClPlayerSkin, Profile.m_aSkin);
	g_Config.m_ClPlayerUseCustomColor = Profile.m_UseCustomColor;
	g_Config.m_ClPlayerColorBody = Profile.m_ColorBody;
	g_Config.m_ClPlayerColorFeet = Profile.m_ColorFeet;

	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		GameClient()->SendInfo(false);
	else
		m_NeedSendinfo = true;
}
void CMenus::ConLoadProfile(IConsole::IResult *pResult, void *pUserData)
{
	CMenus *pSelf = static_cast<CMenus *>(pUserData);
	if(!pSelf->m_ProfilesLoaded)
		pSelf->LoadProfiles();
	const int Index = pResult->GetInteger(0) - 1;
	if(Index < 0 || Index >= (int)pSelf->m_vProfiles.size())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mclient", "no saved profile with that number");
		return;
	}
	pSelf->ApplyProfile(pSelf->m_vProfiles[Index]);
}
void CMenus::OnConsoleInit()
{
	Console()->Register("load_profile", "i[index]", CFGFLAG_CLIENT, ConLoadProfile, this, "M-Client: load a saved player profile by its number in the Profiles list");
}
