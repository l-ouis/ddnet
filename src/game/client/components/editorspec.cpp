/* (c) DDNet contributors. See licence.txt in the root of the distribution for more information. */
#include "editorspec.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/textrender.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/map/render_map.h>

#include <algorithm>
#include <string>

namespace
{
constexpr float TOOL_PALETTE_WIDTH = 470.0f;
constexpr float TOOL_PALETTE_HEIGHT = 54.0f;
constexpr float TOOL_PALETTE_ROUNDING = 10.0f;
constexpr float TOOL_PALETTE_HANDLE_SIZE = TOOL_PALETTE_HEIGHT;
constexpr float TOOL_PALETTE_PADDING = 12.0f;
constexpr float TOOL_BUTTON_WIDTH = 120.0f;
constexpr float TOOL_BUTTON_HEIGHT = 38.0f;
constexpr float TOOL_BUTTON_ROUNDING = 8.0f;
constexpr float TOOL_TOGGLE_WIDTH = 104.0f;
constexpr float TOOL_TOGGLE_HEIGHT = 32.0f;
constexpr float LAYER_DROPDOWN_WIDTH = 110.0f;
constexpr float LAYER_DROPDOWN_HEIGHT = 32.0f;
constexpr float LAYER_DROPDOWN_OPTION_SPACING = 6.0f;
constexpr float LAYER_DROPDOWN_ROUNDING = 8.0f;
constexpr int PRIMARY_TOOL_PAINTBRUSH = 0;
constexpr float TELE_MENU_VERTICAL_GAP = 8.0f;
constexpr float TELE_MENU_HEIGHT = 46.0f;
constexpr float TELE_MENU_ROUNDING = 8.0f;
constexpr float TELE_MENU_PADDING = 12.0f;
constexpr float TELE_NUMBER_BUTTON_WIDTH = 32.0f;
constexpr float TELE_NUMBER_BUTTON_HEIGHT = 30.0f;
constexpr float TELE_NUMBER_INPUT_WIDTH = 72.0f;
constexpr float TELE_NUMBER_INPUT_HEIGHT = 30.0f;
constexpr float TELE_NUMBER_ELEMENT_GAP = 8.0f;
constexpr float TELE_MENU_CONTENT_WIDTH = TELE_NUMBER_BUTTON_WIDTH * 2.0f + TELE_NUMBER_INPUT_WIDTH + TELE_NUMBER_ELEMENT_GAP * 2.0f;
constexpr float TELE_MENU_WIDTH = TELE_MENU_CONTENT_WIDTH + TELE_MENU_PADDING * 2.0f;
constexpr float TELE_INPUT_TEXT_PADDING = 9.0f;
constexpr float TELE_INPUT_HASH_GAP = 6.0f;
constexpr int TELE_NUMBER_MIN = 1;
constexpr int TELE_NUMBER_MAX = 999;

bool PointInRect(const vec2 &Point, const vec2 &TopLeft, const vec2 &Size)
{
	return Point.x >= TopLeft.x && Point.x <= TopLeft.x + Size.x &&
		Point.y >= TopLeft.y && Point.y <= TopLeft.y + Size.y;
}

vec2 ToolPaletteHandlePos(const vec2 &PalettePos)
{
	return PalettePos;
}

vec2 ToolPaletteHandleSize()
{
	return vec2(TOOL_PALETTE_HANDLE_SIZE, TOOL_PALETTE_HANDLE_SIZE);
}

vec2 ToolPalettePrimaryButtonPos(const vec2 &PalettePos)
{
	const float ButtonY = PalettePos.y + (TOOL_PALETTE_HEIGHT - TOOL_BUTTON_HEIGHT) * 0.5f;
	return vec2(PalettePos.x + TOOL_PALETTE_HANDLE_SIZE + TOOL_PALETTE_PADDING, ButtonY);
}

vec2 ToolPalettePrimaryButtonSize()
{
	return vec2(TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT);
}

vec2 ToolPaletteDestructiveButtonPos(const vec2 &PalettePos)
{
	const float ButtonY = PalettePos.y + (TOOL_PALETTE_HEIGHT - TOOL_TOGGLE_HEIGHT) * 0.5f;
	const float ButtonX = PalettePos.x + TOOL_PALETTE_WIDTH - TOOL_PALETTE_PADDING - TOOL_TOGGLE_WIDTH;
	return vec2(ButtonX, ButtonY);
}

vec2 ToolPaletteDestructiveButtonSize()
{
	return vec2(TOOL_TOGGLE_WIDTH, TOOL_TOGGLE_HEIGHT);
}

vec2 ToolPaletteSize()
{
	return vec2(TOOL_PALETTE_WIDTH, TOOL_PALETTE_HEIGHT);
}

vec2 ToolPaletteLayerDropdownPos(const vec2 &PalettePos)
{
	const float DropdownY = PalettePos.y + (TOOL_PALETTE_HEIGHT - LAYER_DROPDOWN_HEIGHT) * 0.5f;
	const float RightOffset = TOOL_PALETTE_PADDING + TOOL_TOGGLE_WIDTH + TOOL_PALETTE_PADDING;
	const float DropdownX = PalettePos.x + TOOL_PALETTE_WIDTH - RightOffset - LAYER_DROPDOWN_WIDTH;
	return vec2(DropdownX, DropdownY);
}

vec2 ToolPaletteLayerDropdownSize()
{
	return vec2(LAYER_DROPDOWN_WIDTH, LAYER_DROPDOWN_HEIGHT);
}

vec2 LayerDropdownOptionsPos(const vec2 &DropdownPos)
{
	return DropdownPos + vec2(0.0f, LAYER_DROPDOWN_HEIGHT + LAYER_DROPDOWN_OPTION_SPACING);
}

vec2 LayerDropdownOptionSize()
{
	return vec2(LAYER_DROPDOWN_WIDTH, LAYER_DROPDOWN_HEIGHT);
}

vec2 TeleMenuSize()
{
	return vec2(TELE_MENU_WIDTH, TELE_MENU_HEIGHT);
}

vec2 TeleMenuPos(const vec2 &PalettePos)
{
	const vec2 DropdownPos = ToolPaletteLayerDropdownPos(PalettePos);
	const vec2 DropdownSize = ToolPaletteLayerDropdownSize();
	const vec2 MenuSize = TeleMenuSize();
	const float CenterX = DropdownPos.x + DropdownSize.x * 0.5f;
	const float MenuX = CenterX - MenuSize.x * 0.5f;
	const float MenuY = PalettePos.y + TOOL_PALETTE_HEIGHT + TELE_MENU_VERTICAL_GAP;
	return vec2(MenuX, MenuY);
}

vec2 TeleNumberRowPos(const vec2 &PalettePos)
{
	const vec2 MenuPos = TeleMenuPos(PalettePos);
	const vec2 MenuSize = TeleMenuSize();
	const float RowWidth = TELE_MENU_CONTENT_WIDTH;
	const float RowX = MenuPos.x + (MenuSize.x - RowWidth) * 0.5f;
	const float RowY = MenuPos.y + (TELE_MENU_HEIGHT - TELE_NUMBER_BUTTON_HEIGHT) * 0.5f;
	return vec2(RowX, RowY);
}

vec2 TeleNumberMinusPos(const vec2 &PalettePos)
{
	return TeleNumberRowPos(PalettePos);
}

vec2 TeleNumberInputPos(const vec2 &PalettePos)
{
	vec2 Pos = TeleNumberRowPos(PalettePos);
	Pos.x += TELE_NUMBER_BUTTON_WIDTH + TELE_NUMBER_ELEMENT_GAP;
	return Pos;
}

vec2 TeleNumberPlusPos(const vec2 &PalettePos)
{
	vec2 Pos = TeleNumberInputPos(PalettePos);
	Pos.x += TELE_NUMBER_INPUT_WIDTH + TELE_NUMBER_ELEMENT_GAP;
	return Pos;
}

vec2 TeleNumberButtonSize()
{
	return vec2(TELE_NUMBER_BUTTON_WIDTH, TELE_NUMBER_BUTTON_HEIGHT);
}

vec2 TeleNumberInputSize()
{
	return vec2(TELE_NUMBER_INPUT_WIDTH, TELE_NUMBER_INPUT_HEIGHT);
}

constexpr const char *gs_apLayerNames[] = {"Game", "Front", "Tele"};

const char *LayerDisplayName(CEditorSpec::ELayerGroup Layer)
{
	const int Index = std::clamp(static_cast<int>(Layer), 0, static_cast<int>(CEditorSpec::ELayerGroup::COUNT) - 1);
	return gs_apLayerNames[Index];
}

CTile *LayerTileData(IMap *pMap, const CMapItemLayerTilemap *pTilemap, int Layer)
{
	if(!pMap || !pTilemap)
	{
		return nullptr;
	}

	int DataIndex = pTilemap->m_Data;
	switch(Layer)
	{
	case LAYER_FRONT:
		if(pTilemap->m_Front >= 0)
		{
			DataIndex = pTilemap->m_Front;
		}
		break;
	case LAYER_GAME:
	default:
		DataIndex = pTilemap->m_Data;
		break;
	}

	if(DataIndex < 0)
	{
		return nullptr;
	}

	const size_t ExpectedSize = (size_t)pTilemap->m_Width * pTilemap->m_Height * sizeof(CTile);
	if(static_cast<size_t>(pMap->GetDataSize(DataIndex)) < ExpectedSize)
	{
		return nullptr;
	}

	return static_cast<CTile *>(pMap->GetData(DataIndex));
}

CTeleTile *TeleLayerData(IMap *pMap, const CMapItemLayerTilemap *pTilemap)
{
	if(!pMap || !pTilemap || pTilemap->m_Tele < 0)
	{
		return nullptr;
	}

	const size_t ExpectedSize = (size_t)pTilemap->m_Width * pTilemap->m_Height * sizeof(CTeleTile);
	if(static_cast<size_t>(pMap->GetDataSize(pTilemap->m_Tele)) < ExpectedSize)
	{
		return nullptr;
	}

	return static_cast<CTeleTile *>(pMap->GetData(pTilemap->m_Tele));
}
}

void CEditorSpec::ResetTeleInputBuffer(SState &State)
{
	str_format(State.m_aTeleInput, sizeof(State.m_aTeleInput), "%d", State.m_SelectedTeleNumber);
	State.m_TeleInputLength = str_length(State.m_aTeleInput);
}

void CEditorSpec::FinishTeleInputEditing(SState &State)
{
	if(!State.m_TeleInputEditing)
		return;
	if(State.m_TeleInputLength > 0)
	{
		int Value = State.m_SelectedTeleNumber;
		if(!str_toint(State.m_aTeleInput, &Value))
		{
			Value = TELE_NUMBER_MIN;
		}
		Value = std::clamp(Value, TELE_NUMBER_MIN, TELE_NUMBER_MAX);
		State.m_SelectedTeleNumber = Value;
	}
	State.m_TeleInputEditing = false;
	State.m_TeleInputOverwrite = false;
	ResetTeleInputBuffer(State);
}

void CEditorSpec::AdjustTeleNumber(SState &State, int Delta)
{
	FinishTeleInputEditing(State);
	const int NewValue = std::clamp(State.m_SelectedTeleNumber + Delta, TELE_NUMBER_MIN, TELE_NUMBER_MAX);
	if(NewValue == State.m_SelectedTeleNumber)
		return;
	State.m_SelectedTeleNumber = NewValue;
	State.m_TeleInputEditing = false;
	State.m_TeleInputOverwrite = false;
	ResetTeleInputBuffer(State);
}

int CEditorSpec::LayerIndexForGroup(ELayerGroup Group) const
{
	switch(Group)
	{
	case ELayerGroup::FRONT:
		return LAYER_FRONT;
	case ELayerGroup::TELE:
		return LAYER_TELE;
	case ELayerGroup::GAME:
	default:
		return LAYER_GAME;
	}
}

bool CEditorSpec::WorldToTile(const vec2 &WorldPos, ivec2 &OutTile) const
{
	const CCollision *pCollision = GameClient()->Collision();
	if(!pCollision)
	{
		return false;
	}
	const int MapWidth = pCollision->GetWidth();
	const int MapHeight = pCollision->GetHeight();
	if(MapWidth <= 0 || MapHeight <= 0)
	{
		return false;
	}
	const int TileX = std::clamp(static_cast<int>(WorldPos.x / 32.0f), 0, MapWidth - 1);
	const int TileY = std::clamp(static_cast<int>(WorldPos.y / 32.0f), 0, MapHeight - 1);
	OutTile = ivec2(TileX, TileY);
	return true;
}

bool CEditorSpec::ComputeSelectionBounds(const vec2 &StartWorld, const vec2 &EndWorld, ivec2 &OutTopLeft, ivec2 &OutSize) const
{
	ivec2 StartTile;
	ivec2 EndTile;
	if(!WorldToTile(StartWorld, StartTile) || !WorldToTile(EndWorld, EndTile))
	{
		return false;
	}
	const int MinX = minimum(StartTile.x, EndTile.x);
	const int MinY = minimum(StartTile.y, EndTile.y);
	const int MaxX = maximum(StartTile.x, EndTile.x);
	const int MaxY = maximum(StartTile.y, EndTile.y);
	OutTopLeft = ivec2(MinX, MinY);
	OutSize = ivec2(MaxX - MinX + 1, MaxY - MinY + 1);
	return OutSize.x > 0 && OutSize.y > 0;
}

bool CEditorSpec::SampleLayerTiles(ELayerGroup Group, const ivec2 &TopLeftTile, const ivec2 &Size, SBrushLayer &OutLayer) const
{
	CLayers *pLayers = GameClient()->Layers();
	if(!pLayers)
	{
		return false;
	}
	CMapItemLayerTilemap *pTilemap = pLayers->GetTilemapForLayer(LayerIndexForGroup(Group));
	IMap *pMap = pLayers->Map();
	if(!pTilemap || !pMap)
	{
		return false;
	}
	CTile *pTiles = LayerTileData(pMap, pTilemap, LayerIndexForGroup(Group));
	if(!pTiles)
	{
		return false;
	}
	OutLayer.m_Group = Group;
	OutLayer.m_Width = Size.x;
	OutLayer.m_Height = Size.y;
	OutLayer.m_Tiles.resize(Size.x * Size.y);
	const int MapWidth = pTilemap->m_Width;
	for(int y = 0; y < Size.y; ++y)
	{
		for(int x = 0; x < Size.x; ++x)
		{
			const int MapIndex = (TopLeftTile.y + y) * MapWidth + (TopLeftTile.x + x);
			const int BrushIndex = y * Size.x + x;
			OutLayer.m_Tiles[BrushIndex].m_Index = pTiles[MapIndex].m_Index;
			OutLayer.m_Tiles[BrushIndex].m_Flags = pTiles[MapIndex].m_Flags;
		}
	}
	if(Group == ELayerGroup::TELE)
	{
		CTeleTile *pTeleTiles = TeleLayerData(pMap, pTilemap);
		if(pTeleTiles)
		{
			OutLayer.m_Tele.resize(Size.x * Size.y);
			for(int y = 0; y < Size.y; ++y)
			{
				for(int x = 0; x < Size.x; ++x)
				{
					const int MapIndex = (TopLeftTile.y + y) * MapWidth + (TopLeftTile.x + x);
					const int BrushIndex = y * Size.x + x;
					OutLayer.m_Tele[BrushIndex].m_Number = pTeleTiles[MapIndex].m_Number;
					OutLayer.m_Tele[BrushIndex].m_Type = pTeleTiles[MapIndex].m_Type;
				}
			}
		}
		else
		{
			OutLayer.m_Tele.clear();
		}
	}
	else
	{
		OutLayer.m_Tele.clear();
	}
	return true;
}

bool CEditorSpec::BrushLayerHasContent(const SBrushLayer &Layer) const
{
	for(const auto &Tile : Layer.m_Tiles)
	{
		if(Tile.m_Index != TILE_AIR)
		{
			return true;
		}
	}
	if(Layer.m_Group == ELayerGroup::TELE)
	{
		for(const auto &Tele : Layer.m_Tele)
		{
			if(Tele.m_Number != 0 && Tele.m_Type != 0)
			{
				return true;
			}
		}
	}
	return false;
}

bool CEditorSpec::CaptureBrushFromSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size)
{
	const int64_t TileCount = int64_t(Size.x) * Size.y;
	if(TileCount <= 0 || TileCount > MAX_TILE_AREA_ITEMS)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Selected brush area is too large");
		return false;
	}

	bool AnyContent = false;
	for(int LayerIndex = 0; LayerIndex < static_cast<int>(ELayerGroup::COUNT); ++LayerIndex)
	{
		SBrushLayer Layer;
		Layer.m_Group = static_cast<ELayerGroup>(LayerIndex);
		if(SampleLayerTiles(Layer.m_Group, TopLeftTile, Size, Layer))
		{
			AnyContent = AnyContent || BrushLayerHasContent(Layer);
			State.m_Brush.m_aLayers[LayerIndex] = std::move(Layer);
		}
		else
		{
			State.m_Brush.m_aLayers[LayerIndex].Clear();
			State.m_Brush.m_aLayers[LayerIndex].m_Group = static_cast<ELayerGroup>(LayerIndex);
		}
	}

	if(!AnyContent)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Selected brush is empty");
		return false;
	}

	State.m_Brush.m_Size = Size;
	State.m_Brush.m_Active = true;
	State.m_BrushPainting = false;
	State.m_LastBrushApplyTile = ivec2(-1, -1);
	return true;
}

void CEditorSpec::ClearBrush(SState &State)
{
	State.m_Brush.Clear();
	State.m_BrushPreviewValid = false;
	State.m_BrushPainting = false;
	State.m_LastBrushApplyTile = ivec2(-1, -1);
}

void CEditorSpec::UpdateBrushPreview(SState &State)
{
	if(!State.m_Brush.m_Active)
	{
		State.m_BrushPreviewValid = false;
		return;
	}
	ivec2 Tile;
	if(!WorldToTile(State.m_CursorWorld, Tile))
	{
		State.m_BrushPreviewValid = false;
		return;
	}
	const CCollision *pCollision = GameClient()->Collision();
	if(!pCollision)
	{
		State.m_BrushPreviewValid = false;
		return;
	}
	const int MapWidth = pCollision->GetWidth();
	const int MapHeight = pCollision->GetHeight();
	if(State.m_Brush.m_Size.x <= 0 || State.m_Brush.m_Size.y <= 0 || MapWidth <= 0 || MapHeight <= 0)
	{
		State.m_BrushPreviewValid = false;
		return;
	}
	const int MaxX = MapWidth - State.m_Brush.m_Size.x;
	const int MaxY = MapHeight - State.m_Brush.m_Size.y;
	if(MaxX < 0 || MaxY < 0)
	{
		State.m_BrushPreviewValid = false;
		return;
	}
	Tile.x = std::clamp(Tile.x, 0, MaxX);
	Tile.y = std::clamp(Tile.y, 0, MaxY);
	State.m_BrushPreviewTile = Tile;
	State.m_BrushPreviewValid = true;
}

bool CEditorSpec::ApplyBrushAtCursor(SState &State)
{
	if(!State.m_Brush.m_Active)
	{
		return false;
	}
	UpdateBrushPreview(State);
	if(!State.m_BrushPreviewValid)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Cannot place brush outside map bounds");
		return false;
	}
	const ivec2 TopLeft = State.m_BrushPreviewTile;
	bool AnySent = false;
	for(int LayerIndex = 0; LayerIndex < static_cast<int>(ELayerGroup::COUNT); ++LayerIndex)
	{
		const SBrushLayer &Layer = State.m_Brush.m_aLayers[LayerIndex];
		if(Layer.m_Width != State.m_Brush.m_Size.x || Layer.m_Height != State.m_Brush.m_Size.y)
		{
			continue;
		}
		const bool LayerHasContent = BrushLayerHasContent(Layer);
		const bool ForceFrontClears = State.m_Destructive && Layer.m_Group == ELayerGroup::FRONT && !Layer.Empty();
		if(!LayerHasContent && !ForceFrontClears)
		{
			continue;
		}
		const size_t TileCount = Layer.m_Tiles.size();
		if(static_cast<ELayerGroup>(LayerIndex) == ELayerGroup::TELE)
		{
			std::vector<CGameClient::STeleTileToolLayer> Payload(TileCount);
			for(size_t i = 0; i < TileCount; ++i)
			{
				Payload[i].m_Index = Layer.m_Tiles[i].m_Index;
				Payload[i].m_Flags = Layer.m_Tiles[i].m_Flags;
				int Number = 0;
				if(i < Layer.m_Tele.size())
				{
					Number = Layer.m_Tele[i].m_Number;
				}
				Payload[i].m_Number = Number;
			}
			if(GameClient()->SendTileToolTelePatternRequest(TopLeft, Layer.m_Width, Layer.m_Height, Payload.data(), Payload.size(), State.m_Destructive))
			{
				AnySent = true;
			}
		}
		else
		{
			std::vector<CGameClient::STileToolLayer> Payload(TileCount);
			for(size_t i = 0; i < TileCount; ++i)
			{
				Payload[i].m_Index = Layer.m_Tiles[i].m_Index;
				Payload[i].m_Flags = Layer.m_Tiles[i].m_Flags;
			}
			const int EngineLayer = LayerIndexForGroup(static_cast<ELayerGroup>(LayerIndex));
			if(GameClient()->SendTileToolPatternRequest(EngineLayer, TopLeft, Layer.m_Width, Layer.m_Height, Payload.data(), Payload.size(), State.m_Destructive))
			{
				AnySent = true;
			}
		}
	}
	return AnySent;
}

void CEditorSpec::RenderBrushOverlay(const SState &State) const
{
	if(State.m_BrushSelecting)
	{
		ivec2 TopLeft;
		ivec2 Size;
		if(ComputeSelectionBounds(State.m_BrushSelectStartWorld, State.m_BrushSelectCurrentWorld, TopLeft, Size))
		{
			const float WorldX = TopLeft.x * 32.0f;
			const float WorldY = TopLeft.y * 32.0f;
			const float Width = Size.x * 32.0f;
			const float Height = Size.y * 32.0f;
			Graphics()->DrawRect(WorldX, WorldY, Width, Height, ColorRGBA(0.2f, 0.6f, 1.0f, 0.25f), 0, 0.0f);
		}
	}
	if(State.m_Brush.m_Active && State.m_BrushPreviewValid)
	{
		const float WorldX = State.m_BrushPreviewTile.x * 32.0f;
		const float WorldY = State.m_BrushPreviewTile.y * 32.0f;
		const float Width = State.m_Brush.m_Size.x * 32.0f;
		const float Height = State.m_Brush.m_Size.y * 32.0f;
		bool RenderedTiles = false;
		CRenderMap *pRenderMap = RenderMap();
		if(pRenderMap)
		{
			const IGraphics::CTextureHandle EntitiesTexture = GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
			Graphics()->TextureSet(EntitiesTexture);
			Graphics()->BlendNormal();
			const auto RenderLayerSamples = [&](ELayerGroup Group, const ColorRGBA &Tint) {
				const int LayerIdx = std::clamp(static_cast<int>(Group), 0, static_cast<int>(ELayerGroup::COUNT) - 1);
				const SBrushLayer &Layer = State.m_Brush.m_aLayers[LayerIdx];
				const int TileCount = static_cast<int>(Layer.m_Tiles.size());
				if(TileCount <= 0 || Layer.m_Width <= 0 || Layer.m_Height <= 0)
				{
					return false;
				}
				bool Any = false;
				Graphics()->SetColor(Tint);
				for(int y = 0; y < Layer.m_Height; ++y)
				{
					for(int x = 0; x < Layer.m_Width; ++x)
					{
						const int BrushIndex = y * Layer.m_Width + x;
						if(BrushIndex < 0 || BrushIndex >= TileCount)
							continue;
						const STileSample &Tile = Layer.m_Tiles[BrushIndex];
						if(Tile.m_Index == TILE_AIR)
							continue;
						const float TileWorldX = (State.m_BrushPreviewTile.x + x) * 32.0f;
						const float TileWorldY = (State.m_BrushPreviewTile.y + y) * 32.0f;
						const unsigned char TileIndex = static_cast<unsigned char>(std::clamp(Tile.m_Index, 0, 255));
						pRenderMap->RenderTile(TileWorldX, TileWorldY, TileIndex, 32.0f, Tint);
						Any = true;
					}
				}
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				return Any;
			};
			if(State.m_SelectedLayer == ELayerGroup::TELE)
			{
				RenderedTiles |= RenderLayerSamples(ELayerGroup::TELE, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			}
			else
			{
				RenderedTiles |= RenderLayerSamples(ELayerGroup::GAME, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
				RenderedTiles |= RenderLayerSamples(ELayerGroup::FRONT, ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f));
			}
		}
		const float HighlightAlpha = RenderedTiles ? 0.08f : 0.35f;
		Graphics()->DrawRect(WorldX, WorldY, Width, Height, ColorRGBA(1.0f, 1.0f, 1.0f, HighlightAlpha), IGraphics::CORNER_ALL, 6.0f);
	}
}

int CEditorSpec::CurrentDummy() const
{
	int Dummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	if(Dummy == 1 && !GameClient()->Client()->DummyConnected())
		Dummy = 0;
	return Dummy;
}

CEditorSpec::SState &CEditorSpec::StateForDummy(int Dummy)
{
	const int Normalized = std::clamp(Dummy, 0, NUM_DUMMIES - 1);
	return m_aStates[Normalized];
}

const CEditorSpec::SState &CEditorSpec::StateForDummy(int Dummy) const
{
	const int Normalized = std::clamp(Dummy, 0, NUM_DUMMIES - 1);
	return m_aStates[Normalized];
}

bool CEditorSpec::IsActive() const
{
	return StateForDummy(CurrentDummy()).m_Active;
}

void CEditorSpec::DeactivateDummy(int Dummy, bool NotifyServer)
{
	const int Normalized = std::clamp(Dummy, 0, NUM_DUMMIES - 1);
	SState &State = m_aStates[Normalized];
	FinishTeleInputEditing(State);
	State.m_ToolPaletteActive = false;
	State.m_ToolPaletteDragging = false;
	State.m_ToolPaletteDragOffset = vec2(0.0f, 0.0f);
	State.m_LayerDropdownOpen = false;
	State.m_TeleInputEditing = false;
	State.m_TeleInputOverwrite = false;
	State.m_BrushSelecting = false;
	State.m_BrushPreviewValid = false;
	State.m_BrushPainting = false;
	State.m_LastBrushApplyTile = ivec2(-1, -1);
	State.m_Brush.Clear();
	if(!State.m_Active)
		return;

	State.m_Active = false;
	State.m_MiddleMousePan = false;
	State.m_LeftMouseHeld = false;
	State.m_CtrlHeld = false;
	GameClient()->SetEditorSpecActive(false, Normalized);
	if(NotifyServer)
		GameClient()->SendEditorSpecState(false, State.m_CursorWorld, Normalized);
	State.m_LastSentCursor = ivec2(0, 0);
}

void CEditorSpec::OnConsoleInit()
{
	Console()->Register("editorspec", "?i[active]", CFGFLAG_CLIENT, ConEditorSpec, this, "Toggle editor spectator mode (1 on / 0 off)");
}

void CEditorSpec::ConEditorSpec(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CEditorSpec *>(pUserData);
	if(pResult->NumArguments() == 0)
	{
		if(pSelf->IsActive())
			pSelf->Deactivate();
		else
			pSelf->Activate();
		return;
	}

	const bool Enable = pResult->GetInteger(0) != 0;
	if(Enable)
		pSelf->Activate();
	else
		pSelf->Deactivate();
}

bool CEditorSpec::CanActivate() const
{
	if(GameClient()->Client()->State() != IClient::STATE_ONLINE)
		return false;
	if(g_Config.m_ClDummy == 1 && !GameClient()->Client()->DummyConnected())
		return false;
	if(GameClient()->m_Snap.m_LocalClientId < 0 || !GameClient()->m_Snap.m_pLocalCharacter)
		return false;
	return true;
}

void CEditorSpec::Activate()
{
	const int Dummy = CurrentDummy();
	SState &State = StateForDummy(Dummy);
	if(State.m_Active)
		return;
	if(!CanActivate())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "Cannot enter editorspec without an active character");
		return;
	}

	State.m_Active = true;
	State.m_MiddleMousePan = false;
	State.m_LeftMouseHeld = false;
	State.m_CtrlHeld = false;
	State.m_CursorWorld = GameClient()->m_LocalCharacterPos;
	State.m_ToolPaletteActive = false;
	State.m_ToolPaletteDragging = false;
	State.m_ToolPaletteDragOffset = vec2(0.0f, 0.0f);
	State.m_LayerDropdownOpen = false;
	State.m_TeleInputEditing = false;
	State.m_TeleInputOverwrite = false;
	State.m_SelectedTeleNumber = std::clamp(State.m_SelectedTeleNumber, TELE_NUMBER_MIN, TELE_NUMBER_MAX);
	ResetTeleInputBuffer(State);
	State.m_LastSentCursor = ivec2(round_to_int(State.m_CursorWorld.x), round_to_int(State.m_CursorWorld.y));
	GameClient()->SetEditorSpecActive(true, Dummy);

	if(!GameClient()->SendEditorSpecState(true, State.m_CursorWorld, Dummy))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "Failed to notify server about editorspec state");
	}
}

void CEditorSpec::Deactivate()
{
	DeactivateDummy(CurrentDummy());
}

void CEditorSpec::OnReset()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		DeactivateDummy(Dummy);
		m_aStates[Dummy] = SState();
	}
}

void CEditorSpec::OnNewSnapshot()
{
	const int Dummy = CurrentDummy();
	const SState &State = StateForDummy(Dummy);
	if(!State.m_Active)
		return;

	if(GameClient()->Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_LocalClientId < 0 || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		DeactivateDummy(Dummy);
	}
}

float CEditorSpec::CursorMoveFactor(IInput::ECursorType CursorType) const
{
	float Factor = 1.0f;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
		Factor = g_Config.m_ClDyncamMousesens / 100.0f;
	else
	{
		switch(CursorType)
		{
		case IInput::CURSOR_MOUSE:
			Factor = g_Config.m_InpMousesens / 100.0f;
			break;
		case IInput::CURSOR_JOYSTICK:
			Factor = g_Config.m_InpControllerSens / 100.0f;
			break;
		default:
			break;
		}
	}

	Factor *= GameClient()->m_Camera.m_Zoom;
	return Factor;
}

void CEditorSpec::MaybeSendCursorUpdate()
{
	const int Dummy = CurrentDummy();
	SState &State = StateForDummy(Dummy);
	if(!State.m_Active)
		return;

	const ivec2 Rounded(round_to_int(State.m_CursorWorld.x), round_to_int(State.m_CursorWorld.y));
	if(Rounded == State.m_LastSentCursor)
		return;

	if(GameClient()->SendEditorSpecState(true, State.m_CursorWorld, Dummy))
		State.m_LastSentCursor = Rounded;
}

bool CEditorSpec::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	const int Dummy = CurrentDummy();
	SState &State = StateForDummy(Dummy);
	if(!State.m_Active)
		return false;

	const float Factor = CursorMoveFactor(CursorType);
	vec2 Delta = vec2(x, y) * Factor;

	if(IsPanning(State))
	{
		const vec2 PanDelta = -Delta;
		GameClient()->m_Controls.m_aMousePos[Dummy] += PanDelta;
		GameClient()->m_Controls.ClampMousePos();
	}
	else
	{
		State.m_CursorWorld += Delta;
	}

	if(State.m_BrushSelecting)
	{
		State.m_BrushSelectCurrentWorld = State.m_CursorWorld;
	}

	if(State.m_ToolPaletteDragging)
	{
		State.m_ToolPalettePos = State.m_CursorWorld - State.m_ToolPaletteDragOffset;
	}

	UpdateBrushPreview(State);

	if(State.m_BrushPainting)
	{
		if(!State.m_BrushPreviewValid)
		{
			State.m_LastBrushApplyTile = ivec2(-1, -1);
		}
		else if(State.m_Brush.m_Active && State.m_BrushPreviewTile != State.m_LastBrushApplyTile)
		{
			if(ApplyBrushAtCursor(State))
			{
				State.m_LastBrushApplyTile = State.m_BrushPreviewTile;
			}
		}
	}

	GameClient()->m_Controls.m_aMouseInputType[Dummy] = CControls::EMouseInputType::RELATIVE;
	MaybeSendCursorUpdate();
	return true;
}

bool CEditorSpec::OnInput(const IInput::CEvent &Event)
{
	const int Dummy = CurrentDummy();
	SState &State = StateForDummy(Dummy);
	const bool Press = (Event.m_Flags & IInput::FLAG_PRESS) != 0;
	const bool Release = (Event.m_Flags & IInput::FLAG_RELEASE) != 0;
	const bool ToolPaletteVisible = State.m_ToolPaletteActive;
	const bool TeleMenuActive = ToolPaletteVisible && State.m_SelectedLayer == ELayerGroup::TELE;
	const vec2 TeleInputPos = TeleNumberInputPos(State.m_ToolPalettePos);
	const vec2 TeleInputSize = TeleNumberInputSize();
	const bool CursorInTeleInput = TeleMenuActive && PointInRect(State.m_CursorWorld, TeleInputPos, TeleInputSize);
	const auto CommitBrushSelection = [&]() {
		if(!State.m_BrushSelecting)
		{
			return;
		}
		ivec2 TopLeftTile;
		ivec2 Size;
		if(ComputeSelectionBounds(State.m_BrushSelectStartWorld, State.m_BrushSelectCurrentWorld, TopLeftTile, Size))
		{
			if(CaptureBrushFromSelection(State, TopLeftTile, Size))
			{
				UpdateBrushPreview(State);
			}
		}
		State.m_BrushSelecting = false;
	};

	if(State.m_TeleInputEditing && (Event.m_Flags & IInput::FLAG_TEXT))
	{
		if(State.m_TeleInputOverwrite)
		{
			State.m_TeleInputLength = 0;
			State.m_aTeleInput[0] = '\0';
			State.m_TeleInputOverwrite = false;
		}
		for(size_t i = 0; i < sizeof(Event.m_aText) && Event.m_aText[i]; ++i)
		{
			const char c = Event.m_aText[i];
			if(c < '0' || c > '9')
				continue;
			if(State.m_TeleInputLength >= TELE_INPUT_BUFFER_SIZE - 1)
				break;
			State.m_aTeleInput[State.m_TeleInputLength++] = c;
			State.m_aTeleInput[State.m_TeleInputLength] = '\0';
		}
		return true;
	}

	if(State.m_TeleInputEditing && (Event.m_Key == KEY_BACKSPACE || Event.m_Key == KEY_DELETE) && Press)
	{
		if(State.m_TeleInputOverwrite)
		{
			State.m_TeleInputLength = 0;
			State.m_aTeleInput[0] = '\0';
			State.m_TeleInputOverwrite = false;
		}
		else if(State.m_TeleInputLength > 0)
		{
			--State.m_TeleInputLength;
			State.m_aTeleInput[State.m_TeleInputLength] = '\0';
		}
		return true;
	}

	if(State.m_TeleInputEditing && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER) && Press)
	{
		FinishTeleInputEditing(State);
		return true;
	}

	if(!State.m_Active)
	{
		if(Event.m_Key == KEY_MOUSE_1 && Release)
			State.m_ToolPaletteDragging = false;
		return false;
	}

	if(Event.m_Key == KEY_D && Press && State.m_CtrlHeld)
	{
		State.m_Destructive = !State.m_Destructive;
		return true;
	}

	if(Event.m_Key == KEY_MOUSE_3)
	{
		if(!State.m_Active)
			return false;
		if(Press)
		{
			State.m_MiddleMousePan = true;
		}
		else if(Release)
		{
			State.m_MiddleMousePan = false;
		}
		return true;
	}

	if(Event.m_Key == KEY_MOUSE_2)
	{
		if(!State.m_Active)
			return false;
		if(Press)
		{
			if(State.m_CtrlHeld)
			{
				FinishTeleInputEditing(State);
				State.m_ToolPaletteDragging = false;
				State.m_BrushSelecting = false;
				State.m_BrushPainting = false;
				State.m_LastBrushApplyTile = ivec2(-1, -1);
				State.m_ToolPaletteActive = !State.m_ToolPaletteActive;
				if(State.m_ToolPaletteActive)
				{
					State.m_ToolPalettePos = State.m_CursorWorld;
					State.m_LayerDropdownOpen = false;
					State.m_TeleInputEditing = false;
					State.m_TeleInputOverwrite = false;
				}
				else
				{
					State.m_LayerDropdownOpen = false;
					State.m_TeleInputEditing = false;
					State.m_TeleInputOverwrite = false;
				}
			}
			else
			{
				State.m_BrushSelecting = false;
				ClearBrush(State);
			}
		}
		return true;
	}

	if(Event.m_Key == KEY_MOUSE_1)
	{
		bool Consumed = false;

		if(Press)
			State.m_LeftMouseHeld = true;
		else if(Release)
			State.m_LeftMouseHeld = false;

		if(Press && State.m_TeleInputEditing && (!TeleMenuActive || !CursorInTeleInput))
			FinishTeleInputEditing(State);

		const vec2 PaletteSize(TOOL_PALETTE_WIDTH, TOOL_PALETTE_HEIGHT);
		const bool CursorInPaletteBody = State.m_ToolPaletteActive && PointInRect(State.m_CursorWorld, State.m_ToolPalettePos, PaletteSize);
		const vec2 DropdownPos = ToolPaletteLayerDropdownPos(State.m_ToolPalettePos);
		const vec2 DropdownSize = ToolPaletteLayerDropdownSize();
		const vec2 DropdownOptionsPos = LayerDropdownOptionsPos(DropdownPos);
		const vec2 DropdownOptionSize = LayerDropdownOptionSize();
		const int LayerCount = static_cast<int>(ELayerGroup::COUNT);
		const vec2 DropdownOptionsSize(DropdownOptionSize.x, DropdownOptionSize.y * LayerCount);
		const bool CursorInDropdownButton = State.m_ToolPaletteActive && PointInRect(State.m_CursorWorld, DropdownPos, DropdownSize);
		const bool CursorInDropdownOptions = State.m_LayerDropdownOpen && PointInRect(State.m_CursorWorld, DropdownOptionsPos, DropdownOptionsSize);
		const vec2 TogglePos = ToolPaletteDestructiveButtonPos(State.m_ToolPalettePos);
		const vec2 ToggleSize = ToolPaletteDestructiveButtonSize();
		const bool CursorInDestructiveToggle = State.m_ToolPaletteActive && PointInRect(State.m_CursorWorld, TogglePos, ToggleSize);
		const bool CursorOverToolPaletteUi = CursorInPaletteBody || CursorInDropdownOptions || CursorInDestructiveToggle;

		if(Press && State.m_ToolPaletteActive)
		{
			const vec2 HandlePos = ToolPaletteHandlePos(State.m_ToolPalettePos);
			const vec2 HandleSize = ToolPaletteHandleSize();
			if(PointInRect(State.m_CursorWorld, HandlePos, HandleSize))
			{
				State.m_ToolPaletteDragging = true;
				State.m_ToolPaletteDragOffset = State.m_CursorWorld - State.m_ToolPalettePos;
				Consumed = true;
			}
		}
		if(!Consumed && State.m_ToolPaletteActive && Press)
		{
			const vec2 PrimaryButtonPos = ToolPalettePrimaryButtonPos(State.m_ToolPalettePos);
			const vec2 PrimaryButtonSize = ToolPalettePrimaryButtonSize();
			if(PointInRect(State.m_CursorWorld, PrimaryButtonPos, PrimaryButtonSize))
			{
				State.m_SelectedPrimaryTool = PRIMARY_TOOL_PAINTBRUSH;
				Consumed = true;
			}
		}
		if(!Consumed && State.m_ToolPaletteActive && Press && CursorInDestructiveToggle)
		{
			State.m_Destructive = !State.m_Destructive;
			Consumed = true;
		}
		if(!Consumed && State.m_ToolPaletteActive && CursorInDropdownButton && Press)
		{
			State.m_LayerDropdownOpen = !State.m_LayerDropdownOpen;
			Consumed = true;
		}
		if(!Consumed && State.m_LayerDropdownOpen && CursorInDropdownOptions && Press)
		{
			Consumed = true;
		}
		if(!Consumed && TeleMenuActive && Press)
		{
			const vec2 MinusPos = TeleNumberMinusPos(State.m_ToolPalettePos);
			const vec2 PlusPos = TeleNumberPlusPos(State.m_ToolPalettePos);
			const vec2 ButtonDims = TeleNumberButtonSize();
			if(PointInRect(State.m_CursorWorld, MinusPos, ButtonDims))
			{
				AdjustTeleNumber(State, -1);
				Consumed = true;
			}
			else if(PointInRect(State.m_CursorWorld, PlusPos, ButtonDims))
			{
				AdjustTeleNumber(State, 1);
				Consumed = true;
			}
			else if(CursorInTeleInput)
			{
				State.m_TeleInputEditing = true;
				State.m_TeleInputOverwrite = false;
				Consumed = true;
			}
		}
		else if(Release)
		{
			if(State.m_ToolPaletteDragging)
			{
				State.m_ToolPaletteDragging = false;
				Consumed = true;
			}
			if(State.m_LayerDropdownOpen)
			{
				if(CursorInDropdownOptions)
				{
					const float LocalY = State.m_CursorWorld.y - DropdownOptionsPos.y;
					int SelectedIndex = static_cast<int>(LocalY / DropdownOptionSize.y);
					SelectedIndex = std::clamp(SelectedIndex, 0, LayerCount - 1);
					const ELayerGroup NewLayer = static_cast<ELayerGroup>(SelectedIndex);
					if(State.m_SelectedLayer != NewLayer)
					{
						if(State.m_SelectedLayer == ELayerGroup::TELE)
							FinishTeleInputEditing(State);
						State.m_SelectedLayer = NewLayer;
						if(State.m_SelectedLayer != ELayerGroup::TELE)
						{
							State.m_TeleInputEditing = false;
							State.m_TeleInputOverwrite = false;
						}
					}
					State.m_LayerDropdownOpen = false;
					Consumed = true;
				}
				else if(!CursorInDropdownButton)
				{
					State.m_LayerDropdownOpen = false;
				}
			}
		}

		if(Consumed)
			return true;

		if(!State.m_Active)
			return State.m_CtrlHeld;

		const bool PaintbrushEnabled = State.m_SelectedPrimaryTool == PRIMARY_TOOL_PAINTBRUSH && !State.m_CtrlHeld && !CursorOverToolPaletteUi;

		if(PaintbrushEnabled && State.m_Brush.m_Active)
		{
			if(Press)
			{
				State.m_BrushPainting = true;
				State.m_LastBrushApplyTile = ivec2(-1, -1);
				if(State.m_BrushPreviewValid && ApplyBrushAtCursor(State))
				{
					State.m_LastBrushApplyTile = State.m_BrushPreviewTile;
				}
				return true;
			}
			if(Release && State.m_BrushPainting)
			{
				State.m_BrushPainting = false;
				State.m_LastBrushApplyTile = ivec2(-1, -1);
				return true;
			}
			if(State.m_BrushPainting)
			{
				return true;
			}
		}

		if(PaintbrushEnabled && !State.m_Brush.m_Active)
		{
			if(Press)
			{
				State.m_BrushPainting = false;
				State.m_LastBrushApplyTile = ivec2(-1, -1);
				State.m_BrushSelecting = true;
				State.m_BrushSelectStartWorld = State.m_CursorWorld;
				State.m_BrushSelectCurrentWorld = State.m_CursorWorld;
				return true;
			}
			if(Release && State.m_BrushSelecting)
			{
				CommitBrushSelection();
				return true;
			}
		}

		return State.m_CtrlHeld;
	}

	if(Event.m_Key == KEY_LCTRL || Event.m_Key == KEY_RCTRL)
	{
		if(Press)
		{
			State.m_CtrlHeld = true;
			State.m_BrushPainting = false;
			State.m_LastBrushApplyTile = ivec2(-1, -1);
		}
		else if(Release)
			State.m_CtrlHeld = false;
		return false;
	}

	return false;
}

void CEditorSpec::OnRender()
{
	const SState &State = StateForDummy(CurrentDummy());
	if(GameClient()->Client()->State() != IClient::STATE_ONLINE)
		return;

	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);

	float aPoints[4];
	Graphics()->MapScreenToWorld(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, 100.0f, 100.0f, 100.0f,
		0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	const int ActiveLocalClientId = GameClient()->ClientIdForDummy(CurrentDummy());

	const float CursorSize = 48.0f;
	const float NameFontSize = 16.0f;
	const float NameOffset = CursorSize + 6.0f;
	const auto RenderRemoteCursor = [&](const vec2 &WorldPos) {
		Graphics()->WrapClamp();
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_CURSOR].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
		IGraphics::CQuadItem Quad(WorldPos.x, WorldPos.y, CursorSize, CursorSize);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};

	const ColorRGBA NameColor(1.0f, 1.0f, 1.0f, 0.95f);
	const ColorRGBA NameOutline(0.0f, 0.0f, 0.0f, 0.6f);
	std::string RemoteCursorLog;
	TextRender()->TextColor(NameColor);
	TextRender()->TextOutlineColor(NameOutline);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !ClientData.m_EditorSpecCursorActive || (ActiveLocalClientId >= 0 && ClientId == ActiveLocalClientId))
		{
			continue;
		}
		RenderRemoteCursor(ClientData.m_EditorSpecCursor);
		if(ClientData.m_aName[0] == '\0')
		{
			continue;
		}
		const float TextWidth = TextRender()->TextWidth(NameFontSize, ClientData.m_aName);
		const float TextX = ClientData.m_EditorSpecCursor.x + CursorSize * 0.5f - TextWidth * 0.5f;
		const float TextY = ClientData.m_EditorSpecCursor.y + NameOffset;
		TextRender()->Text(TextX, TextY, NameFontSize, ClientData.m_aName);
		char aLogEntry[128];
		const char *pName = ClientData.m_aName[0] ? ClientData.m_aName : "unnamed";
		str_format(aLogEntry, sizeof(aLogEntry), "%s(%.1f, %.1f)", pName, ClientData.m_EditorSpecCursor.x, ClientData.m_EditorSpecCursor.y);
		if(!RemoteCursorLog.empty())
		{
			RemoteCursorLog.append(", ");
		}
		RemoteCursorLog.append(aLogEntry);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	const auto RenderToolPalette = [&]() {
		const vec2 PaletteSize(TOOL_PALETTE_WIDTH, TOOL_PALETTE_HEIGHT);
		const vec2 HandlePos = ToolPaletteHandlePos(State.m_ToolPalettePos);
		const vec2 HandleSize = ToolPaletteHandleSize();
		Graphics()->DrawRect(State.m_ToolPalettePos.x, State.m_ToolPalettePos.y, PaletteSize.x, PaletteSize.y, ColorRGBA(0.04f, 0.04f, 0.04f, 0.85f), IGraphics::CORNER_ALL, TOOL_PALETTE_ROUNDING);
		Graphics()->DrawRect(HandlePos.x, HandlePos.y, HandleSize.x, HandleSize.y, ColorRGBA(0.95f, 0.55f, 0.25f, 0.95f), IGraphics::CORNER_ALL, 6.0f);

		const vec2 PrimaryButtonPos = ToolPalettePrimaryButtonPos(State.m_ToolPalettePos);
		const vec2 PrimaryButtonSize = ToolPalettePrimaryButtonSize();
		const bool SelectedPaintbrush = State.m_SelectedPrimaryTool == PRIMARY_TOOL_PAINTBRUSH;
		const ColorRGBA ButtonColor = SelectedPaintbrush ? ColorRGBA(0.2f, 0.55f, 1.0f, 0.95f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.85f);
		Graphics()->DrawRect(PrimaryButtonPos.x, PrimaryButtonPos.y, PrimaryButtonSize.x, PrimaryButtonSize.y, ButtonColor, IGraphics::CORNER_ALL, TOOL_BUTTON_ROUNDING);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const float IconFontSize = PrimaryButtonSize.y * 0.55f;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const float IconWidth = TextRender()->TextWidth(IconFontSize, FontIcons::FONT_ICON_PEN_TO_SQUARE);
		const float IconX = PrimaryButtonPos.x + (PrimaryButtonSize.x - IconWidth) * 0.5f;
		const float IconY = PrimaryButtonPos.y + (PrimaryButtonSize.y - IconFontSize) * 0.5f;
		TextRender()->Text(IconX, IconY, IconFontSize, FontIcons::FONT_ICON_PEN_TO_SQUARE);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		const vec2 TogglePos = ToolPaletteDestructiveButtonPos(State.m_ToolPalettePos);
		const vec2 ToggleSize = ToolPaletteDestructiveButtonSize();
		const bool ToggleHover = PointInRect(State.m_CursorWorld, TogglePos, ToggleSize);
		ColorRGBA ToggleColor;
		if(State.m_Destructive)
		{
			ToggleColor = ToggleHover ? ColorRGBA(0.95f, 0.35f, 0.35f, 0.95f) : ColorRGBA(0.85f, 0.25f, 0.25f, 0.9f);
		}
		else
		{
			ToggleColor = ToggleHover ? ColorRGBA(0.35f, 0.75f, 0.35f, 0.95f) : ColorRGBA(0.25f, 0.6f, 0.25f, 0.9f);
		}
		Graphics()->DrawRect(TogglePos.x, TogglePos.y, ToggleSize.x, ToggleSize.y, ToggleColor, IGraphics::CORNER_ALL, TOOL_BUTTON_ROUNDING);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const char *pToggleText = State.m_Destructive ? "Destructive" : "Additive";
		const float ToggleFontSize = 13.0f;
		const float ToggleTextY = TogglePos.y + (ToggleSize.y - ToggleFontSize) * 0.5f;
		TextRender()->Text(TogglePos.x + 10.0f, ToggleTextY, ToggleFontSize, pToggleText);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const vec2 DropdownPos = ToolPaletteLayerDropdownPos(State.m_ToolPalettePos);
		const vec2 DropdownSize = ToolPaletteLayerDropdownSize();
		const bool DropdownHover = PointInRect(State.m_CursorWorld, DropdownPos, DropdownSize);
		ColorRGBA DropdownColor;
		if(State.m_LayerDropdownOpen)
		{
			DropdownColor = ColorRGBA(0.2f, 0.55f, 1.0f, 0.95f);
		}
		else if(DropdownHover)
		{
			DropdownColor = ColorRGBA(0.35f, 0.35f, 0.35f, 0.85f);
		}
		else
		{
			DropdownColor = ColorRGBA(0.18f, 0.18f, 0.18f, 0.75f);
		}
		Graphics()->DrawRect(DropdownPos.x, DropdownPos.y, DropdownSize.x, DropdownSize.y, DropdownColor, IGraphics::CORNER_ALL, LAYER_DROPDOWN_ROUNDING);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));
		const float LayerTextY = DropdownPos.y + (DropdownSize.y - 14.0f) * 0.5f;
		TextRender()->Text(DropdownPos.x + 12.0f, LayerTextY, 14.0f, LayerDisplayName(State.m_SelectedLayer));
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const float ChevronSize = 13.0f;
		const float ChevronX = DropdownPos.x + DropdownSize.x - 18.0f;
		const float ChevronY = DropdownPos.y + (DropdownSize.y - ChevronSize) * 0.5f;
		TextRender()->Text(ChevronX, ChevronY, ChevronSize, FontIcons::FONT_ICON_CHEVRON_DOWN);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	};

	const auto RenderLayerDropdownOptions = [&]() {
		if(!State.m_LayerDropdownOpen)
			return;
		const vec2 DropdownPos = ToolPaletteLayerDropdownPos(State.m_ToolPalettePos);
		const vec2 OptionSize = LayerDropdownOptionSize();
		const vec2 OptionsPos = LayerDropdownOptionsPos(DropdownPos);
		const int LayerCount = static_cast<int>(ELayerGroup::COUNT);
		for(int i = 0; i < LayerCount; ++i)
		{
			const vec2 RowPos = OptionsPos + vec2(0.0f, OptionSize.y * i);
			const bool Hover = PointInRect(State.m_CursorWorld, RowPos, OptionSize);
			const bool Selected = State.m_SelectedLayer == static_cast<ELayerGroup>(i);
			ColorRGBA RowColor;
			if(Selected && Hover)
				RowColor = ColorRGBA(0.2f, 0.55f, 1.0f, 0.95f);
			else if(Selected)
				RowColor = ColorRGBA(0.2f, 0.55f, 1.0f, 0.8f);
			else if(Hover)
				RowColor = ColorRGBA(0.35f, 0.35f, 0.35f, 0.9f);
			else
				RowColor = ColorRGBA(0.18f, 0.18f, 0.18f, 0.85f);
			int Corners = 0;
			if(i == 0)
				Corners |= IGraphics::CORNER_T;
			if(i == LayerCount - 1)
				Corners |= IGraphics::CORNER_B;
			const float Rounding = Corners ? LAYER_DROPDOWN_ROUNDING : 0.0f;
			Graphics()->DrawRect(RowPos.x, RowPos.y, OptionSize.x, OptionSize.y, RowColor, Corners, Rounding);
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));
			const float TextY = RowPos.y + (OptionSize.y - 14.0f) * 0.5f;
			TextRender()->Text(RowPos.x + 10.0f, TextY, 14.0f, LayerDisplayName(static_cast<ELayerGroup>(i)));
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	};

	const auto RenderTeleMenu = [&]() {
		if(State.m_SelectedLayer != ELayerGroup::TELE)
			return;
		const vec2 MenuPos = TeleMenuPos(State.m_ToolPalettePos);
		const vec2 MenuSize = TeleMenuSize();
		Graphics()->DrawRect(MenuPos.x, MenuPos.y, MenuSize.x, MenuSize.y, ColorRGBA(0.05f, 0.05f, 0.05f, 0.9f), IGraphics::CORNER_ALL, TELE_MENU_ROUNDING);
		const vec2 MinusPos = TeleNumberMinusPos(State.m_ToolPalettePos);
		const vec2 PlusPos = TeleNumberPlusPos(State.m_ToolPalettePos);
		const vec2 ButtonSizeTele = TeleNumberButtonSize();
		const vec2 InputPos = TeleNumberInputPos(State.m_ToolPalettePos);
		const vec2 InputSize = TeleNumberInputSize();
		const bool MinusHover = PointInRect(State.m_CursorWorld, MinusPos, ButtonSizeTele);
		const bool PlusHover = PointInRect(State.m_CursorWorld, PlusPos, ButtonSizeTele);
		const bool InputHover = PointInRect(State.m_CursorWorld, InputPos, InputSize);
		const auto RenderTeleButton = [&](const vec2 &Pos, bool Hovered, const char *pIcon) {
			const ColorRGBA Color = Hovered ? ColorRGBA(0.35f, 0.35f, 0.35f, 0.95f) : ColorRGBA(0.22f, 0.22f, 0.22f, 0.85f);
			Graphics()->DrawRect(Pos.x, Pos.y, ButtonSizeTele.x, ButtonSizeTele.y, Color, IGraphics::CORNER_ALL, 6.0f);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const float IconSize = ButtonSizeTele.y * 0.6f;
			const float IconXPos = Pos.x + (ButtonSizeTele.x - IconSize) * 0.5f;
			const float IconYPos = Pos.y + (ButtonSizeTele.y - IconSize) * 0.5f;
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			TextRender()->Text(IconXPos, IconYPos, IconSize, pIcon);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		RenderTeleButton(MinusPos, MinusHover, FontIcons::FONT_ICON_MINUS);
		RenderTeleButton(PlusPos, PlusHover, FontIcons::FONT_ICON_PLUS);
		ColorRGBA InputColor;
		if(State.m_TeleInputEditing)
			InputColor = ColorRGBA(0.2f, 0.45f, 0.85f, 0.95f);
		else if(InputHover)
			InputColor = ColorRGBA(0.25f, 0.25f, 0.25f, 0.9f);
		else
			InputColor = ColorRGBA(0.18f, 0.18f, 0.18f, 0.8f);
		Graphics()->DrawRect(InputPos.x, InputPos.y, InputSize.x, InputSize.y, InputColor, IGraphics::CORNER_ALL, 6.0f);
		const float InputFontSize = 16.0f;
		const float HashX = InputPos.x + TELE_INPUT_TEXT_PADDING;
		const float HashY = InputPos.y + (InputSize.y - InputFontSize) * 0.5f;
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f));
		TextRender()->Text(HashX, HashY, InputFontSize, "#");
		const float HashWidth = TextRender()->TextWidth(InputFontSize, "#");
		char aNumberBuf[16];
		const char *pNumberText = nullptr;
		if(State.m_TeleInputEditing)
		{
			pNumberText = State.m_TeleInputLength > 0 ? State.m_aTeleInput : "";
		}
		else
		{
			str_format(aNumberBuf, sizeof(aNumberBuf), "%d", State.m_SelectedTeleNumber);
			pNumberText = aNumberBuf;
		}
		if(pNumberText && pNumberText[0] != '\0')
		{
			const float NumberX = HashX + HashWidth + TELE_INPUT_HASH_GAP;
			TextRender()->Text(NumberX, HashY, InputFontSize, pNumberText);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	};

	RenderBrushOverlay(State);

	if(State.m_ToolPaletteActive)
	{
		RenderToolPalette();
		if(State.m_SelectedLayer == ELayerGroup::TELE)
			RenderTeleMenu();
		RenderLayerDropdownOptions();
	}
    
	if(State.m_Active)
	{
		GameClient()->RenderTools()->RenderCursor(State.m_CursorWorld, 48.0f);
	}

	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}
