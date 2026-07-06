#ifndef GAME_CLIENT_COMPONENTS_ADS_H
#define GAME_CLIENT_COMPONENTS_ADS_H

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <vector>

class CAds : public CComponent
{
	struct SAd
	{
		IGraphics::CTextureHandle m_Texture;
		float m_AspectRatio = 1.0f;
	};
	std::vector<SAd> m_vAds;

	bool m_Active = false;
	int m_CurrentAd = -1;
	int m_RotationIndex = 0;
	vec2 m_MousePos = vec2(0.0f, 0.0f);
	float m_NextAdTime = 0.0f;

	struct SLayout
	{
		CUIRect m_Window;
		CUIRect m_TitleBar;
		CUIRect m_Image;
		CUIRect m_CloseButton;
	};
	SLayout ComputeLayout() const;

	void Show();
	void Close();
	void ScheduleNext();

	static int AdScan(const char *pName, int IsDir, int DirType, void *pUser);
	static void ConShowAd(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnReset() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool IsActive() const { return m_Active; }
};

#endif
