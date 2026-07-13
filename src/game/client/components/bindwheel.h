#ifndef GAME_CLIENT_COMPONENTS_BINDWHEEL_H
#define GAME_CLIENT_COMPONENTS_BINDWHEEL_H

#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/ui.h>

#include <vector>

class CBindWheel : public CComponent
{
public:
	static constexpr int MIN_BINDS = 8;
	static constexpr int MAX_BINDS = 16;

	struct CBind
	{
		char m_aName[64] = "";
		char m_aCommand[256] = "";
	};

	std::vector<CBind> m_vBinds;

	static int NumSlots();

private:
	bool m_WasActive = false;
	bool m_Active = false;

	vec2 m_SelectorMouse = vec2(0.0f, 0.0f);
	int m_SelectedBind = -1;

	static void ConKeyBindWheel(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnConsoleInit() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const { return m_Active; }

	void LoadBinds();
};

#endif
