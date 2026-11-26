#ifndef GAME_CLIENT_COMPONENTS_MAP_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_MAP_PREVIEW_H

#include <base/vmath.h>

#include <engine/map.h>
#include <engine/storage.h>

#include <game/client/component.h>
#include <game/client/components/envelope_state.h>
#include <game/client/components/mapimages.h>
#include <game/layers.h>
#include <game/map/map_renderer.h>
#include <game/map/render_layer.h>

#include <memory>
#include <string>

class IHttpRequest;

class CMapPreview : public CComponent
{
	enum class EState
	{
		INACTIVE,
		DOWNLOADING,
		ACTIVE,
		FAILED,
	};

	EState m_State = EState::INACTIVE;
	std::string m_MapName;
	char m_aFileName[IO_MAX_PATH_LENGTH] = "";
	char m_aTempPath[IO_MAX_PATH_LENGTH] = "";
	std::shared_ptr<IHttpRequest> m_pDownload;

	std::unique_ptr<IMap> m_pMap;
	CLayers m_Layers;
	CMapImages m_Images;
	CEnvelopeState m_EnvEvaluator;
	CMapRenderer m_MapRenderer;
	CRenderLayerParams m_Params;
	bool m_Loaded = false;

	vec2 m_Center = vec2(0.0f, 0.0f);
	vec2 m_MapSize = vec2(0.0f, 0.0f);

	float m_SavedZoom = 1.0f;
	float m_SavedUserZoomTarget = 1.0f;

	int m_EntityOverlay = 0;
	bool m_HasPreviewZoom = false;
	float m_PreviewZoom = 1.0f;

	bool IsPreviewBind(const IInput::CEvent &Event) const;
	void RunBindWithPreviewEntities(const IInput::CEvent &Event);

	bool FindLocalMap(char *pPath, int PathSize, int *pStorageType) const;
	void StartDownload();
	void PollDownload();
	bool LoadMap(const char *pPath, int StorageType);
	void UnloadMap();
	void Close(bool ReopenMenu);
	void RenderPreview();
	void RenderOverlay();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	void Open(const char *pMapName);
	bool IsActive() const { return m_State != EState::INACTIVE; }
};

#endif
