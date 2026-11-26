#include "map_preview.h"

#include <base/color.h>
#include <base/hash.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>

static constexpr const char *DOWNLOADED_MAPS_FOLDER = "downloadedmaps";

void CMapPreview::OnInit()
{
	m_pMap = CreateMap();
	m_Images.OnInterfacesInit(GameClient());

	m_Params.m_RenderType = ERenderType::RENDERTYPE_FOREGROUND;
	m_Params.m_EntityOverlayVal = 0;
	m_Params.m_RenderInvalidTiles = false;
	m_Params.m_RenderTileBorder = true;
	m_Params.m_RenderText = false;
	m_Params.m_DebugRenderGroupClips = false;
	m_Params.m_DebugRenderQuadClips = false;
	m_Params.m_DebugRenderClusterClips = false;
	m_Params.m_DebugRenderTileClips = false;
}

void CMapPreview::OnShutdown()
{
	Close(false);
}

void CMapPreview::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && IsActive())
		Close(false);
}

void CMapPreview::Open(const char *pMapName)
{
	Close(false);
	if(pMapName == nullptr || pMapName[0] == '\0')
		return;

	m_MapName = pMapName;
	str_copy(m_aFileName, pMapName);
	str_sanitize_filename(m_aFileName);
	str_format(m_aTempPath, sizeof(m_aTempPath), "%s/%s_preview.tmp", DOWNLOADED_MAPS_FOLDER, m_aFileName);

	GameClient()->m_Menus.SetActive(false);

	CCamera &Camera = GameClient()->m_Camera;
	m_SavedZoom = Camera.m_Zooming ? Camera.m_ZoomSmoothingTarget : Camera.m_Zoom;
	m_SavedUserZoomTarget = Camera.m_UserZoomTarget;
	if(m_HasPreviewZoom)
	{
		Camera.m_Zoom = m_PreviewZoom;
		Camera.m_ZoomSmoothingTarget = m_PreviewZoom;
		Camera.m_Zooming = false;
		Camera.m_UserZoomTarget = m_PreviewZoom;
	}

	char aLocalPath[IO_MAX_PATH_LENGTH];
	int StorageType;
	if(FindLocalMap(aLocalPath, sizeof(aLocalPath), &StorageType) && LoadMap(aLocalPath, StorageType))
	{
		m_State = EState::ACTIVE;
		return;
	}
	StartDownload();
}

struct SDownloadedMapSearch
{
	const char *m_pMapName;
	char m_aPath[IO_MAX_PATH_LENGTH];
	int m_StorageType;
	time_t m_TimeModified;
};

static int FindDownloadedMapCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	SDownloadedMapSearch *pSearch = static_cast<SDownloadedMapSearch *>(pUser);
	if(IsDir)
		return 0;

	const char *pRest = str_startswith(pInfo->m_pName, pSearch->m_pMapName);
	if(pRest == nullptr || pRest[0] != '_' || !str_endswith(pRest, ".map"))
		return 0;
	char aHash[SHA256_MAXSTRSIZE];
	const int HashLength = str_length(pRest) - 1 - str_length(".map");
	if(HashLength != (int)SHA256_MAXSTRSIZE - 1 && HashLength != 8)
		return 0;
	str_truncate(aHash, sizeof(aHash), pRest + 1, HashLength);
	if(!str_isallnum_hex(aHash))
		return 0;

	if(pSearch->m_aPath[0] == '\0' || pInfo->m_TimeModified > pSearch->m_TimeModified)
	{
		str_format(pSearch->m_aPath, sizeof(pSearch->m_aPath), "%s/%s", DOWNLOADED_MAPS_FOLDER, pInfo->m_pName);
		pSearch->m_StorageType = StorageType;
		pSearch->m_TimeModified = pInfo->m_TimeModified;
	}
	return 0;
}

bool CMapPreview::FindLocalMap(char *pPath, int PathSize, int *pStorageType) const
{
	SDownloadedMapSearch Search;
	Search.m_pMapName = m_MapName.c_str();
	Search.m_aPath[0] = '\0';
	Search.m_StorageType = IStorage::TYPE_ALL;
	Search.m_TimeModified = 0;
	Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, DOWNLOADED_MAPS_FOLDER, FindDownloadedMapCallback, &Search);
	if(Search.m_aPath[0] != '\0')
	{
		str_copy(pPath, Search.m_aPath, PathSize);
		*pStorageType = Search.m_StorageType;
		return true;
	}

	char aMapsPath[IO_MAX_PATH_LENGTH];
	str_format(aMapsPath, sizeof(aMapsPath), "maps/%s.map", m_MapName.c_str());
	if(Storage()->FileExists(aMapsPath, IStorage::TYPE_ALL))
	{
		str_copy(pPath, aMapsPath, PathSize);
		*pStorageType = IStorage::TYPE_ALL;
		return true;
	}
	return false;
}

void CMapPreview::StartDownload()
{
	Storage()->CreateFolder(DOWNLOADED_MAPS_FOLDER, IStorage::TYPE_SAVE);
	char aEscaped[3 * MAX_MAP_LENGTH + 1];
	EscapeUrl(aEscaped, m_MapName.c_str());
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/mappreview/%s.map", aEscaped);

	m_pDownload = HttpGetFile(aUrl, Storage(), m_aTempPath, IStorage::TYPE_SAVE);
	m_pDownload->Timeout(CTimeout{10000, 0, 500, 10});
	m_pDownload->MaxResponseSize(128 * 1024 * 1024);
	m_pDownload->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pDownload);
	m_State = EState::DOWNLOADING;
}

void CMapPreview::Close(bool ReopenMenu)
{
	if(m_pDownload)
	{
		m_pDownload->Abort();
		m_pDownload = nullptr;
		Storage()->RemoveFile(m_aTempPath, IStorage::TYPE_SAVE);
	}
	UnloadMap();
	const bool WasActive = IsActive();
	m_State = EState::INACTIVE;
	if(WasActive)
	{
		CCamera &Camera = GameClient()->m_Camera;
		m_PreviewZoom = Camera.m_Zooming ? Camera.m_ZoomSmoothingTarget : Camera.m_Zoom;
		m_HasPreviewZoom = true;
		Camera.m_Zoom = m_SavedZoom;
		Camera.m_ZoomSmoothingTarget = m_SavedZoom;
		Camera.m_Zooming = false;
		Camera.m_UserZoomTarget = m_SavedUserZoomTarget;
	}
	if(ReopenMenu && WasActive && Client()->State() == IClient::STATE_ONLINE)
		GameClient()->m_Menus.SetActive(true);
}

void CMapPreview::PollDownload()
{
	if(!m_pDownload || !m_pDownload->Done())
		return;

	const bool Success = m_pDownload->State() == EHttpState::DONE;
	m_pDownload = nullptr;
	if(!Success)
	{
		Storage()->RemoveFile(m_aTempPath, IStorage::TYPE_SAVE);
		m_State = EState::FAILED;
		return;
	}

	SHA256_DIGEST Sha256;
	if(!Storage()->CalculateHashes(m_aTempPath, IStorage::TYPE_SAVE, &Sha256))
	{
		Storage()->RemoveFile(m_aTempPath, IStorage::TYPE_SAVE);
		m_State = EState::FAILED;
		return;
	}
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(Sha256, aSha256, sizeof(aSha256));
	char aMapPath[IO_MAX_PATH_LENGTH];
	str_format(aMapPath, sizeof(aMapPath), "%s/%s_%s.map", DOWNLOADED_MAPS_FOLDER, m_aFileName, aSha256);

	bool Created = false;
	if(Storage()->FileExists(aMapPath, IStorage::TYPE_SAVE))
	{
		Storage()->RemoveFile(m_aTempPath, IStorage::TYPE_SAVE);
	}
	else if(Storage()->RenameFile(m_aTempPath, aMapPath, IStorage::TYPE_SAVE))
	{
		Created = true;
	}
	else
	{
		Storage()->RemoveFile(m_aTempPath, IStorage::TYPE_SAVE);
		m_State = EState::FAILED;
		return;
	}

	if(LoadMap(aMapPath, IStorage::TYPE_SAVE))
	{
		m_State = EState::ACTIVE;
		return;
	}
	if(Created)
		Storage()->RemoveFile(aMapPath, IStorage::TYPE_SAVE);
	m_State = EState::FAILED;
}

bool CMapPreview::LoadMap(const char *pPath, int StorageType)
{
	UnloadMap();
	if(!m_pMap->Load(Storage(), pPath, StorageType))
	{
		log_error("map_preview", "failed to load '%s'", pPath);
		return false;
	}

	m_Layers.Init(m_pMap.get(), false, true);
	if(m_Layers.GameLayer() == nullptr)
	{
		log_error("map_preview", "'%s' has no game layer", pPath);
		m_Layers.Unload();
		m_pMap->Unload();
		return false;
	}

	m_Images.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	m_Images.LoadBackground(&m_Layers, m_pMap.get());

	m_EnvEvaluator = CEnvelopeState(m_pMap.get(), false);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
	m_MapRenderer.OnInit(Graphics(), TextRender(), RenderMap());
	m_MapRenderer.Load(ERenderType::RENDERTYPE_FULL_DESIGN, &m_Layers, &m_Images, &m_EnvEvaluator, std::nullopt, true);

	const CMapItemLayerTilemap *pGameLayer = m_Layers.GameLayer();
	m_MapSize = vec2(pGameLayer->m_Width * 32.0f, pGameLayer->m_Height * 32.0f);
	m_Center = m_MapSize / 2.0f;
	const CTile *pTiles = static_cast<const CTile *>(m_pMap->GetData(pGameLayer->m_Data));
	if(pTiles != nullptr)
	{
		const int NumTiles = pGameLayer->m_Width * pGameLayer->m_Height;
		for(int Index = 0; Index < NumTiles; Index++)
		{
			const int Tile = pTiles[Index].m_Index;
			if(Tile >= ENTITY_OFFSET + ENTITY_SPAWN && Tile <= ENTITY_OFFSET + ENTITY_SPAWN_BLUE)
			{
				m_Center = vec2((Index % pGameLayer->m_Width) * 32.0f + 16.0f, (Index / pGameLayer->m_Width) * 32.0f + 16.0f);
				break;
			}
		}
	}
	m_Loaded = true;
	return true;
}

void CMapPreview::UnloadMap()
{
	if(!m_Loaded)
		return;
	m_MapRenderer.Clear();
	m_Images.Unload();
	m_Layers.Unload();
	m_pMap->Unload();
	m_Loaded = false;
}

bool CMapPreview::IsPreviewBind(const IInput::CEvent &Event) const
{
	const int ModifierMask = CBinds::GetModifierMask(Input()) & ~CBinds::GetModifierMaskOfKey(Event.m_Key);
	const char *pBind = GameClient()->m_Binds.Get(Event.m_Key, ModifierMask);
	if(pBind == nullptr)
		pBind = GameClient()->m_Binds.Get(Event.m_Key, KeyModifier::NONE);
	if(pBind == nullptr || pBind[0] == '\0')
		return false;

	char aBind[512];
	str_copy(aBind, pBind);
	for(char *pCommand = aBind; pCommand != nullptr;)
	{
		char *pNext = (char *)str_find(pCommand, ";");
		if(pNext != nullptr)
			*pNext++ = '\0';
		const char *pStart = str_skip_whitespaces(pCommand);
		char aName[64] = "";
		const char *pArgs = str_next_token(pStart, " ", aName, sizeof(aName));
		const bool Zoom = str_comp(aName, "zoom") == 0 || str_comp(aName, "zoom+") == 0 || str_comp(aName, "zoom-") == 0;
		const bool Entities = str_comp(aName, "cl_overlay_entities") == 0 ||
				      ((str_comp(aName, "toggle") == 0 || str_comp(aName, "+toggle") == 0) && pArgs != nullptr && str_find(pArgs, "cl_overlay_entities") == str_skip_whitespaces_const(pArgs));
		if(pStart[0] != '\0' && !Zoom && !Entities)
			return false;
		pCommand = pNext;
	}
	return true;
}

void CMapPreview::RunBindWithPreviewEntities(const IInput::CEvent &Event)
{
	const int RealOverlay = g_Config.m_ClOverlayEntities;
	g_Config.m_ClOverlayEntities = m_EntityOverlay;
	GameClient()->m_Binds.OnInput(Event);
	m_EntityOverlay = g_Config.m_ClOverlayEntities;
	g_Config.m_ClOverlayEntities = RealOverlay;
}

bool CMapPreview::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(!(Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(m_State == EState::ACTIVE && (Event.m_Flags & IInput::FLAG_RELEASE))
			RunBindWithPreviewEntities(Event);
	}
	else
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			Close(true);
			return true;
		}
		if(m_State == EState::ACTIVE)
		{
			if(IsPreviewBind(Event))
				RunBindWithPreviewEntities(Event);
			else if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
				Console()->ExecuteLine("zoom+", IConsole::CLIENT_ID_UNSPECIFIED);
			else if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
				Console()->ExecuteLine("zoom-", IConsole::CLIENT_ID_UNSPECIFIED);
		}
	}
	return true;
}

bool CMapPreview::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;
	if(m_State == EState::ACTIVE && CursorType == IInput::CURSOR_MOUSE)
	{
		float Factor = g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens ? g_Config.m_ClDyncamMousesens / 100.0f : g_Config.m_InpMousesens / 100.0f;
		Factor *= GameClient()->m_Camera.m_Zoom;
		m_Center += vec2(x, y) * Factor;
		m_Center.x = std::clamp(m_Center.x, -201.0f * 32.0f, m_MapSize.x + 201.0f * 32.0f);
		m_Center.y = std::clamp(m_Center.y, -201.0f * 32.0f, m_MapSize.y + 201.0f * 32.0f);
	}
	return true;
}

void CMapPreview::OnRender()
{
	if(!IsActive())
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Close(false);
		return;
	}

	PollDownload();

	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(m_EntityOverlay ? g_Config.m_ClBackgroundEntitiesColor : g_Config.m_ClBackgroundColor));
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Graphics()->ScreenWidth(), Graphics()->ScreenHeight()));
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(BackgroundColor.r, BackgroundColor.g, BackgroundColor.b, 1.0f);
	IGraphics::CQuadItem Quad(0.0f, 0.0f, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();

	if(m_State == EState::ACTIVE)
		RenderPreview();
	else
		RenderOverlay();
}

void CMapPreview::RenderPreview()
{
	m_EnvEvaluator.Update();
	m_Params.m_Center = m_Center;
	m_Params.m_Zoom = GameClient()->m_Camera.m_Zoom;
	m_Params.m_EntityOverlayVal = m_EntityOverlay;
	m_MapRenderer.Render(m_Params);
}

void CMapPreview::RenderOverlay()
{
	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();

	CUIRect Box;
	Screen.Margin(vec2((Screen.w - 360.0f) / 2.0f, (Screen.h - 70.0f) / 2.0f), &Box);
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 8.0f);
	CUIRect Title, Hint;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(28.0f, &Title, &Hint);

	char aBuf[MAX_MAP_LENGTH + 128];
	if(m_State == EState::DOWNLOADING)
	{
		CUIRect Spinner;
		Title.VSplitLeft(24.0f, &Spinner, &Title);
		Ui()->RenderProgressSpinner(Spinner.Center(), 8.0f);
		const int Progress = m_pDownload ? m_pDownload->Progress() : 0;
		str_format(aBuf, sizeof(aBuf), "%s %s… %d%%", Localize("Downloading"), m_MapName.c_str(), Progress);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s %s", Localize("Couldn't load"), m_MapName.c_str());
	}
	SLabelProperties Props;
	Props.m_MaxWidth = Title.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Title, aBuf, 14.0f, TEXTALIGN_ML, Props);
	Ui()->DoLabel(&Hint, Localize("Press ESC to go back"), 10.0f, TEXTALIGN_ML);
}
