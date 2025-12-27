/* (c) DDNet contributors. See licence.txt in the root of the distribution for more information. */
#include "editorspec.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/storage.h>
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
#include <cmath>
#include <string>

namespace
{
constexpr float TOOL_PALETTE_WIDTH = 720.0f;
constexpr float TOOL_PALETTE_HEIGHT = 54.0f;
constexpr float TOOL_PALETTE_ROUNDING = 10.0f;
constexpr float TOOL_PALETTE_HANDLE_SIZE = TOOL_PALETTE_HEIGHT;
constexpr float TOOL_PALETTE_PADDING = 12.0f;
constexpr float TOOL_BUTTON_WIDTH = 120.0f;
constexpr float TOOL_BUTTON_HEIGHT = 38.0f;
constexpr float TOOL_BUTTON_ROUNDING = 8.0f;
constexpr float TOOL_BUTTON_GAP = 14.0f;
constexpr float TOOL_TOGGLE_WIDTH = 104.0f;
constexpr float TOOL_TOGGLE_HEIGHT = 32.0f;
constexpr float TOOL_TOGGLE_GAP = 10.0f;
constexpr float LAYER_DROPDOWN_WIDTH = 110.0f;
constexpr float LAYER_DROPDOWN_HEIGHT = 32.0f;
constexpr float LAYER_DROPDOWN_OPTION_SPACING = 6.0f;
constexpr float LAYER_DROPDOWN_ROUNDING = 8.0f;
constexpr int PRIMARY_TOOL_PAINTBRUSH = 0;
constexpr int PRIMARY_TOOL_DRAW = 1;
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
constexpr int TELE_NUMBER_MAX = 256;
constexpr int BRUSH_PICKER_COLS = 16;
constexpr int BRUSH_PICKER_ROWS = 16;
constexpr float BRUSH_PICKER_BASE_TILE_SIZE = 32.0f;
constexpr int DRAW_SAMPLE_TICK_STEP = 2;
constexpr int DRAW_SEGMENT_LIFETIME_SECONDS = 60;
constexpr size_t MAX_STORED_DRAW_SEGMENTS = 1024;
constexpr int MAX_DRAW_SEGMENTS_PER_PACKET = 32;
constexpr size_t MAX_STORED_DRAW_TEXTS = 512;
constexpr size_t MAX_DRAW_TEXT_LENGTH = 256;
const ColorRGBA DRAW_DEFAULT_COLOR(0.3f, 0.1f, 0.1f, 1.0f);
constexpr float DRAW_LINE_HALF_WIDTH = 2.0f;

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

vec2 ToolPaletteSecondaryButtonPos(const vec2 &PalettePos)
{
	vec2 Pos = ToolPalettePrimaryButtonPos(PalettePos);
	Pos.x += TOOL_BUTTON_WIDTH + TOOL_BUTTON_GAP;
	return Pos;
}

vec2 ToolPaletteShowDiffButtonPos(const vec2 &PalettePos)
{
	const float ButtonY = PalettePos.y + (TOOL_PALETTE_HEIGHT - TOOL_TOGGLE_HEIGHT) * 0.5f;
	const float ButtonX = PalettePos.x + TOOL_PALETTE_WIDTH - TOOL_PALETTE_PADDING - TOOL_TOGGLE_WIDTH;
	return vec2(ButtonX, ButtonY);
}

vec2 ToolPaletteShowDiffButtonSize()
{
	return vec2(TOOL_TOGGLE_WIDTH, TOOL_TOGGLE_HEIGHT);
}

vec2 ToolPaletteDestructiveButtonPos(const vec2 &PalettePos)
{
	vec2 Pos = ToolPaletteShowDiffButtonPos(PalettePos);
	Pos.x -= TOOL_TOGGLE_WIDTH + TOOL_TOGGLE_GAP;
	return Pos;
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
	const float ToggleClusterWidth = TOOL_TOGGLE_WIDTH * 2.0f + TOOL_TOGGLE_GAP;
	const float RightOffset = TOOL_PALETTE_PADDING + ToggleClusterWidth + TOOL_PALETTE_PADDING;
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

template<typename SampleType, typename TransformFunc>
void RemapBrushSamples(const std::vector<SampleType> &Source, std::vector<SampleType> &Destination, int OldWidth, int OldHeight, int NewWidth, int NewHeight, const TransformFunc &MapFunc)
{
	for(int y = 0; y < OldHeight; ++y)
	{
		for(int x = 0; x < OldWidth; ++x)
		{
			const ivec2 NewPos = MapFunc(x, y, OldWidth, OldHeight);
			if(NewPos.x < 0 || NewPos.x >= NewWidth || NewPos.y < 0 || NewPos.y >= NewHeight)
			{
				continue;
			}
			const int OldIndex = y * OldWidth + x;
			const int NewIndex = NewPos.y * NewWidth + NewPos.x;
			Destination[NewIndex] = Source[OldIndex];
		}
	}
}

int RotateTileFlagsCW(int Flags)
{
	if(Flags & TILEFLAG_ROTATE)
	{
		Flags ^= (TILEFLAG_YFLIP | TILEFLAG_XFLIP);
	}
	Flags ^= TILEFLAG_ROTATE;
	return Flags;
}

int RotateTileFlags(int Flags, bool Clockwise)
{
	int Steps = Clockwise ? 1 : 3;
	Steps %= 4;
	for(int i = 0; i < Steps; ++i)
	{
		Flags = RotateTileFlagsCW(Flags);
	}
	return Flags;
}

int FlipTileFlagsHorizontal(int Flags)
{
	return Flags ^ ((Flags & TILEFLAG_ROTATE) ? TILEFLAG_YFLIP : TILEFLAG_XFLIP);
}

int FlipTileFlagsVertical(int Flags)
{
	return Flags ^ ((Flags & TILEFLAG_ROTATE) ? TILEFLAG_XFLIP : TILEFLAG_YFLIP);
}
}

const char *CEditorSpec::BrushPickerEntitiesName() const
{
	return "DDNet";
	// const CGameInfo &Info = GameClient()->m_GameInfo;
	// if(Info.m_EntitiesFDDrace)
	// 	return "F-DDrace";
	// if(Info.m_EntitiesFNG)
	// 	return "FNG";
	// if(Info.m_EntitiesBW)
	// 	return "blockworlds";
	// if(Info.m_EntitiesRace)
	// 	return "Race";
	// if(Info.m_EntitiesVanilla)
	// 	return "Vanilla";
	// return "DDNet";
}

int CEditorSpec::BrushPickerTextureLoadFlags() const
{
	return Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
}

IGraphics::CTextureHandle CEditorSpec::BrushPickerTexture(ELayerGroup Layer) const
{
	const int TextureFlags = BrushPickerTextureLoadFlags();
	const auto EnsureTexture = [&](IGraphics::CTextureHandle &Handle, const char *pPath) -> IGraphics::CTextureHandle {
		if(!Handle.IsValid())
		{
			Handle = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL, TextureFlags);
		}
		return Handle;
	};

	switch(Layer)
	{
	case ELayerGroup::FRONT:
		return EnsureTexture(m_BrushPickerFrontTexture, "editor/front.png");
	case ELayerGroup::TELE:
		return EnsureTexture(m_BrushPickerTeleTexture, "editor/tele.png");
	case ELayerGroup::GAME:
	default:
	{
		const char *pEntitiesName = BrushPickerEntitiesName();
		if(m_aBrushPickerEntitiesName[0] == '\0' || str_comp(m_aBrushPickerEntitiesName, pEntitiesName) != 0)
		{
			if(m_BrushPickerGameTexture.IsValid())
			{
				Graphics()->UnloadTexture(&m_BrushPickerGameTexture);
			}
			str_copy(m_aBrushPickerEntitiesName, pEntitiesName, sizeof(m_aBrushPickerEntitiesName));
		}
		if(!m_BrushPickerGameTexture.IsValid())
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "editor/entities/%s.png", m_aBrushPickerEntitiesName);
			m_BrushPickerGameTexture = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL, TextureFlags);
			if(!m_BrushPickerGameTexture.IsValid() && str_comp(m_aBrushPickerEntitiesName, "DDNet") != 0)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "editorspec", "Failed to load entities tileset, falling back to DDNet");
				str_copy(m_aBrushPickerEntitiesName, "DDNet", sizeof(m_aBrushPickerEntitiesName));
				str_format(aPath, sizeof(aPath), "editor/entities/%s.png", m_aBrushPickerEntitiesName);
				m_BrushPickerGameTexture = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL, TextureFlags);
			}
		}
		return m_BrushPickerGameTexture;
	}
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

bool CEditorSpec::TileHasContent(ELayerGroup Group, const STileSample &Tile, const STeleSample &Tele) const
{
	if(Tile.m_Index != TILE_AIR)
	{
		return true;
	}
	if(Group == ELayerGroup::TELE)
	{
		return Tele.m_Number != 0 || Tele.m_Type != 0;
	}
	return false;
}

bool CEditorSpec::BrushPickerVisible(const SState &State) const
{
	return State.m_Active && State.m_SelectedPrimaryTool == PRIMARY_TOOL_PAINTBRUSH && State.m_SpaceHeld && !State.m_CtrlHeld && !State.m_DrawTextEditing;
}

float CEditorSpec::BrushPickerTileSizeWorld() const
{
	const float Zoom = maximum(GameClient()->m_Camera.m_Zoom, 0.01f);
	return BRUSH_PICKER_BASE_TILE_SIZE * Zoom;
}

vec2 CEditorSpec::BrushPickerTopLeft(float TileSize) const
{
	const vec2 PickerSize(BRUSH_PICKER_COLS * TileSize, BRUSH_PICKER_ROWS * TileSize);
	const vec2 Center = GameClient()->m_Camera.m_Center;
	return Center - PickerSize * 0.5f;
}

bool CEditorSpec::BrushPickerCellFromWorld(const SState &State, const vec2 &WorldPos, ivec2 &OutCell) const
{
	if(!BrushPickerVisible(State))
	{
		return false;
	}
	const float TileSize = BrushPickerTileSizeWorld();
	const vec2 TopLeft = BrushPickerTopLeft(TileSize);
	const vec2 Local = WorldPos - TopLeft;
	if(Local.x < 0.0f || Local.y < 0.0f)
	{
		return false;
	}
	const int CellX = static_cast<int>(floor(Local.x / TileSize));
	const int CellY = static_cast<int>(floor(Local.y / TileSize));
	if(CellX < 0 || CellX >= BRUSH_PICKER_COLS || CellY < 0 || CellY >= BRUSH_PICKER_ROWS)
	{
		return false;
	}
	OutCell = ivec2(CellX, CellY);
	return true;
}

bool CEditorSpec::TileIndexValidForLayer(ELayerGroup Layer, int TileIndex) const
{
	switch(Layer)
	{
	case ELayerGroup::FRONT:
		return TileIndex == TILE_AIR || IsValidFrontTile(TileIndex);
	case ELayerGroup::TELE:
		return TileIndex == TILE_AIR || IsValidTeleTile(TileIndex);
	case ELayerGroup::GAME:
	default:
		return TileIndex == TILE_AIR || IsValidGameTile(TileIndex);
	}
}

void CEditorSpec::ApplyBrushPickerSelection(SState &State, const ivec2 &TopLeftCell, const ivec2 &Size)
{
	if(Size.x <= 0 || Size.y <= 0)
	{
		return;
	}
	const int StartX = std::clamp(TopLeftCell.x, 0, BRUSH_PICKER_COLS - 1);
	const int StartY = std::clamp(TopLeftCell.y, 0, BRUSH_PICKER_ROWS - 1);
	const int EndX = std::clamp(TopLeftCell.x + Size.x - 1, 0, BRUSH_PICKER_COLS - 1);
	const int EndY = std::clamp(TopLeftCell.y + Size.y - 1, 0, BRUSH_PICKER_ROWS - 1);
	const int Width = EndX - StartX + 1;
	const int Height = EndY - StartY + 1;
	if(Width <= 0 || Height <= 0)
	{
		return;
	}

	State.m_Brush.Clear();
	State.m_Brush.m_Active = true;
	State.m_Brush.m_Size = ivec2(Width, Height);
	const int LayerIndex = std::clamp(static_cast<int>(State.m_SelectedLayer), 0, static_cast<int>(ELayerGroup::COUNT) - 1);
	for(int i = 0; i < static_cast<int>(ELayerGroup::COUNT); ++i)
	{
		State.m_Brush.m_aLayers[i].m_Group = static_cast<ELayerGroup>(i);
	}
	SBrushLayer &Layer = State.m_Brush.m_aLayers[LayerIndex];
	Layer.m_Group = State.m_SelectedLayer;
	Layer.m_Width = Width;
	Layer.m_Height = Height;
	Layer.m_Tiles.assign(Width * Height, STileSample());
	if(State.m_SelectedLayer == ELayerGroup::TELE)
	{
		Layer.m_Tele.assign(Width * Height, STeleSample());
	}
	else
	{
		Layer.m_Tele.clear();
	}

	for(int y = 0; y < Height; ++y)
	{
		for(int x = 0; x < Width; ++x)
		{
			const int CellX = StartX + x;
			const int CellY = StartY + y;
			const int TileIndex = std::clamp(CellY, 0, BRUSH_PICKER_ROWS - 1) * BRUSH_PICKER_COLS + std::clamp(CellX, 0, BRUSH_PICKER_COLS - 1);
			STileSample Sample{};
			Sample.m_Index = TileIndex;
			Sample.m_Flags = 0;
			if(!TileIndexValidForLayer(State.m_SelectedLayer, Sample.m_Index))
			{
				Sample.m_Index = TILE_AIR;
			}
			Layer.m_Tiles[y * Width + x] = Sample;
		}
	}

	State.m_BrushPreviewValid = false;
	State.m_BrushPainting = false;
	State.m_BrushSelecting = false;
	State.m_AreaSelecting = false;
	State.m_LastBrushApplyTile = ivec2(-1, -1);
	UpdateBrushPreview(State);
}

bool CEditorSpec::CaptureBrushFromSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size)
{
	const int64_t TileCount = int64_t(Size.x) * Size.y;
	if(TileCount <= 0 || TileCount > MAX_TILE_AREA_ITEMS)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Selected brush area is too large");
		return false;
	}

	bool CapturedAnyLayer = false;
	for(int LayerIndex = 0; LayerIndex < static_cast<int>(ELayerGroup::COUNT); ++LayerIndex)
	{
		SBrushLayer Layer;
		Layer.m_Group = static_cast<ELayerGroup>(LayerIndex);
		if(SampleLayerTiles(Layer.m_Group, TopLeftTile, Size, Layer))
		{
			CapturedAnyLayer = true;
			State.m_Brush.m_aLayers[LayerIndex] = std::move(Layer);
		}
		else
		{
			State.m_Brush.m_aLayers[LayerIndex].Clear();
			State.m_Brush.m_aLayers[LayerIndex].m_Group = static_cast<ELayerGroup>(LayerIndex);
		}
	}

	if(!CapturedAnyLayer)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Failed to capture brush data");
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
	State.m_AreaSelecting = false;
	State.m_AreaSelectionUseBrush = false;
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
		const bool ForceClears = State.m_Destructive && Layer.m_Group != ELayerGroup::TELE && !Layer.Empty();
		if(!LayerHasContent && !ForceClears)
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

bool CEditorSpec::ApplyBrushTiledSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size)
{
	if(!State.m_Brush.m_Active)
	{
		return false;
	}
	if(Size.x <= 0 || Size.y <= 0)
	{
		return false;
	}
	const int64_t TileCount64 = int64_t(Size.x) * Size.y;
	if(TileCount64 <= 0 || TileCount64 > MAX_TILE_AREA_ITEMS)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Selected area is too large for tiling");
		return false;
	}

	const int SelectionWidth = Size.x;
	const int SelectionHeight = Size.y;
	const int SelectionTileCount = SelectionWidth * SelectionHeight;
	bool AnySent = false;
	for(int LayerIndex = 0; LayerIndex < static_cast<int>(ELayerGroup::COUNT); ++LayerIndex)
	{
		const SBrushLayer &Layer = State.m_Brush.m_aLayers[LayerIndex];
		if(Layer.m_Width <= 0 || Layer.m_Height <= 0 || Layer.Empty())
		{
			continue;
		}
		const bool LayerHasContent = BrushLayerHasContent(Layer);
		const bool ForceClears = State.m_Destructive && Layer.m_Group != ELayerGroup::TELE;
		if(!LayerHasContent && !ForceClears)
		{
			continue;
		}
		const ELayerGroup Group = static_cast<ELayerGroup>(LayerIndex);
		if(Group == ELayerGroup::TELE)
		{
			if(static_cast<int>(Layer.m_Tele.size()) < Layer.m_Width * Layer.m_Height)
			{
				continue;
			}
			std::vector<CGameClient::STeleTileToolLayer> Payload(SelectionTileCount);
			for(int y = 0; y < SelectionHeight; ++y)
			{
				for(int x = 0; x < SelectionWidth; ++x)
				{
					const int BrushX = x % Layer.m_Width;
					const int BrushY = y % Layer.m_Height;
					const int BrushIndex = BrushY * Layer.m_Width + BrushX;
					const int OutIndex = y * SelectionWidth + x;
					const STileSample &Sample = Layer.m_Tiles[BrushIndex];
					auto &Out = Payload[OutIndex];
					Out.m_Index = Sample.m_Index;
					Out.m_Flags = Sample.m_Flags;
					int Number = 0;
					if(BrushIndex < (int)Layer.m_Tele.size())
					{
						Number = Layer.m_Tele[BrushIndex].m_Number;
					}
					Out.m_Number = Number;
				}
			}
			if(GameClient()->SendTileToolTelePatternRequest(TopLeftTile, SelectionWidth, SelectionHeight, Payload.data(), SelectionTileCount, State.m_Destructive))
			{
				AnySent = true;
			}
			continue;
		}

		std::vector<CGameClient::STileToolLayer> Payload(SelectionTileCount);
		for(int y = 0; y < SelectionHeight; ++y)
		{
			for(int x = 0; x < SelectionWidth; ++x)
			{
				const int BrushX = x % Layer.m_Width;
				const int BrushY = y % Layer.m_Height;
				const int BrushIndex = BrushY * Layer.m_Width + BrushX;
				const int OutIndex = y * SelectionWidth + x;
				const STileSample &Sample = Layer.m_Tiles[BrushIndex];
				Payload[OutIndex].m_Index = Sample.m_Index;
				Payload[OutIndex].m_Flags = Sample.m_Flags;
			}
		}
		const int EngineLayer = LayerIndexForGroup(Group);
		if(GameClient()->SendTileToolPatternRequest(EngineLayer, TopLeftTile, SelectionWidth, SelectionHeight, Payload.data(), SelectionTileCount, State.m_Destructive))
		{
			AnySent = true;
		}
	}

	return AnySent;
}


bool CEditorSpec::ApplyDestructiveAirSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size, ELayerGroup Layer)
{
	if(Size.x <= 0 || Size.y <= 0)
	{
		return false;
	}
	const int64_t TileCount64 = int64_t(Size.x) * Size.y;
	if(TileCount64 <= 0 || TileCount64 > MAX_TILE_AREA_ITEMS)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editorspec", "Selected area is too large to edit");
		return false;
	}

	if(Layer == ELayerGroup::TELE)
	{
		const int TileCount = Size.x * Size.y;
		std::vector<CGameClient::STeleTileToolLayer> Payload(TileCount);
		for(auto &Cell : Payload)
		{
			Cell.m_Index = TILE_AIR;
			Cell.m_Flags = 0;
			Cell.m_Number = 0;
		}
		return GameClient()->SendTileToolTelePatternRequest(TopLeftTile, Size.x, Size.y, Payload.data(), TileCount, true);
	}

	// When additive mode is active on the front layer, treat shift-drag as a hookthrough fill instead of a clear.
	const bool PlaceHookthrough = Layer == ELayerGroup::FRONT && !State.m_Destructive;
	CGameClient::STileToolLayer FillTile;
	FillTile.m_Index = PlaceHookthrough ? TILE_THROUGH_CUT : TILE_AIR;
	FillTile.m_Flags = 0;
	if(State.m_Destructive && Layer == ELayerGroup::FRONT)
	{
		// Force clears of hookthrough overrides by blasting both layers with air.
		bool Success = GameClient()->SendTileToolAreaFillRequest(LAYER_FRONT, TopLeftTile, Size.x, Size.y, FillTile);
		CGameClient::STileToolLayer GameAir;
		GameAir.m_Index = TILE_AIR;
		GameAir.m_Flags = 0;
		Success = GameClient()->SendTileToolAreaFillRequest(LAYER_GAME, TopLeftTile, Size.x, Size.y, GameAir) || Success;
		return Success;
	}
	const int EngineLayer = LayerIndexForGroup(Layer);
	return GameClient()->SendTileToolAreaFillRequest(EngineLayer, TopLeftTile, Size.x, Size.y, FillTile);
}

bool CEditorSpec::RotateBrush(SState &State, bool Clockwise)
{
	if(!State.m_Brush.m_Active)
	{
		return false;
	}
	const int BrushWidth = State.m_Brush.m_Size.x;
	const int BrushHeight = State.m_Brush.m_Size.y;
	if(BrushWidth <= 0 || BrushHeight <= 0)
	{
		return false;
	}

	const auto MapFunc = [Clockwise](int x, int y, int OldWidth, int OldHeight) -> ivec2 {
		if(Clockwise)
		{
			return ivec2(OldHeight - 1 - y, x);
		}
		return ivec2(y, OldWidth - 1 - x);
	};

	bool Modified = false;
	for(auto &Layer : State.m_Brush.m_aLayers)
	{
		const int OldWidth = Layer.m_Width;
		const int OldHeight = Layer.m_Height;
		if(OldWidth <= 0 || OldHeight <= 0)
		{
			continue;
		}
		const int NewWidth = OldHeight;
		const int NewHeight = OldWidth;
		const size_t NewTileCount = (size_t)NewWidth * NewHeight;
		const size_t OldTileCount = (size_t)OldWidth * OldHeight;
		std::vector<STileSample> NewTiles(NewTileCount, STileSample());
		if(Layer.m_Tiles.size() == OldTileCount)
		{
			RemapBrushSamples(Layer.m_Tiles, NewTiles, OldWidth, OldHeight, NewWidth, NewHeight, MapFunc);
		}
		Layer.m_Tiles = std::move(NewTiles);
		for(auto &Tile : Layer.m_Tiles)
		{
			if(Tile.m_Index == TILE_AIR)
			{
				Tile.m_Flags = 0;
				continue;
			}
			if(!IsRotatableTile(Tile.m_Index))
			{
				Tile.m_Flags = 0;
				continue;
			}
			Tile.m_Flags = RotateTileFlags(Tile.m_Flags, Clockwise);
		}
		if(!Layer.m_Tele.empty())
		{
			std::vector<STeleSample> NewTele(NewTileCount, STeleSample());
			if(Layer.m_Tele.size() == OldTileCount)
			{
				RemapBrushSamples(Layer.m_Tele, NewTele, OldWidth, OldHeight, NewWidth, NewHeight, MapFunc);
			}
			Layer.m_Tele = std::move(NewTele);
		}
		Layer.m_Width = NewWidth;
		Layer.m_Height = NewHeight;
		Modified = true;
	}

	if(!Modified)
	{
		return false;
	}

	State.m_Brush.m_Size = ivec2(BrushHeight, BrushWidth);
	State.m_LastBrushApplyTile = ivec2(-1, -1);
	UpdateBrushPreview(State);
	return true;
}

bool CEditorSpec::MirrorBrush(SState &State, bool Horizontal)
{
	if(!State.m_Brush.m_Active)
	{
		return false;
	}
	const int BrushWidth = State.m_Brush.m_Size.x;
	const int BrushHeight = State.m_Brush.m_Size.y;
	if(BrushWidth <= 0 || BrushHeight <= 0)
	{
		return false;
	}

	const auto MapFunc = [Horizontal](int x, int y, int OldWidth, int OldHeight) -> ivec2 {
		if(Horizontal)
		{
			return ivec2(OldWidth - 1 - x, y);
		}
		return ivec2(x, OldHeight - 1 - y);
	};

	bool Modified = false;
	for(auto &Layer : State.m_Brush.m_aLayers)
	{
		const int OldWidth = Layer.m_Width;
		const int OldHeight = Layer.m_Height;
		if(OldWidth <= 0 || OldHeight <= 0)
		{
			continue;
		}
		const size_t TileCount = (size_t)OldWidth * OldHeight;
		std::vector<STileSample> NewTiles(TileCount, STileSample());
		if(Layer.m_Tiles.size() == TileCount)
		{
			RemapBrushSamples(Layer.m_Tiles, NewTiles, OldWidth, OldHeight, OldWidth, OldHeight, MapFunc);
		}
		Layer.m_Tiles = std::move(NewTiles);
		for(auto &Tile : Layer.m_Tiles)
		{
			if(Tile.m_Index == TILE_AIR)
			{
				Tile.m_Flags = 0;
				continue;
			}
			if(!IsRotatableTile(Tile.m_Index))
			{
				Tile.m_Flags = 0;
				continue;
			}
			Tile.m_Flags = Horizontal ? FlipTileFlagsHorizontal(Tile.m_Flags) : FlipTileFlagsVertical(Tile.m_Flags);
		}
		if(!Layer.m_Tele.empty())
		{
			std::vector<STeleSample> NewTele(TileCount, STeleSample());
			if(Layer.m_Tele.size() == TileCount)
			{
				RemapBrushSamples(Layer.m_Tele, NewTele, OldWidth, OldHeight, OldWidth, OldHeight, MapFunc);
			}
			Layer.m_Tele = std::move(NewTele);
		}
		Modified = true;
	}

	if(!Modified)
	{
		return false;
	}

	State.m_LastBrushApplyTile = ivec2(-1, -1);
	UpdateBrushPreview(State);
	return true;
}

void CEditorSpec::RenderBrushOverlay(const SState &State) const
{
	const auto RenderSelectionRect = [&](const vec2 &StartWorld, const vec2 &EndWorld, const ColorRGBA &Tint) {
		ivec2 TopLeft;
		ivec2 Size;
		if(!ComputeSelectionBounds(StartWorld, EndWorld, TopLeft, Size))
		{
			return;
		}
		const float WorldX = TopLeft.x * 32.0f;
		const float WorldY = TopLeft.y * 32.0f;
		const float Width = Size.x * 32.0f;
		const float Height = Size.y * 32.0f;
		Graphics()->DrawRect(WorldX, WorldY, Width, Height, Tint, 0, 0.0f);
		const auto RenderBorder = [&](float X, float Y, float W, float H) {
			const float MaxThickness = 2.0f;
			const float Thickness = minimum(MaxThickness, minimum(W * 0.5f, H * 0.5f));
			if(Thickness <= 0.0f)
			{
				return;
			}
			const ColorRGBA BorderColor(1.0f, 1.0f, 1.0f, 0.5f);
			Graphics()->DrawRect(X, Y, W, Thickness, BorderColor, 0, 0.0f);
			Graphics()->DrawRect(X, Y + H - Thickness, W, Thickness, BorderColor, 0, 0.0f);
			const float SideHeight = maximum(H - Thickness * 2.0f, 0.0f);
			if(SideHeight <= 0.0f)
			{
				return;
			}
			Graphics()->DrawRect(X, Y + Thickness, Thickness, SideHeight, BorderColor, 0, 0.0f);
			Graphics()->DrawRect(X + W - Thickness, Y + Thickness, Thickness, SideHeight, BorderColor, 0, 0.0f);
		};
		RenderBorder(WorldX, WorldY, Width, Height);
	};

	if(State.m_BrushSelecting)
	{
		RenderSelectionRect(State.m_BrushSelectStartWorld, State.m_BrushSelectCurrentWorld, ColorRGBA(0.2f, 0.6f, 1.0f, 0.25f));
	}
	if(State.m_AreaSelecting)
	{
		RenderSelectionRect(State.m_AreaSelectStartWorld, State.m_AreaSelectCurrentWorld, ColorRGBA(0.95f, 0.5f, 0.1f, 0.28f));
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
						const unsigned char TileFlags = static_cast<unsigned char>(std::clamp(Tile.m_Flags, 0, 255));
						pRenderMap->RenderTile(TileWorldX, TileWorldY, TileIndex, 32.0f, Tint, TileFlags);
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

	void CEditorSpec::RenderBrushPicker(const SState &State) const
	{
		if(!BrushPickerVisible(State))
		{
			return;
		}
		const float TileSize = BrushPickerTileSizeWorld();
		const vec2 GridSize(BRUSH_PICKER_COLS * TileSize, BRUSH_PICKER_ROWS * TileSize);
		const vec2 TopLeft = BrushPickerTopLeft(TileSize);
		float ViewX0, ViewY0, ViewX1, ViewY1;
		Graphics()->GetScreen(&ViewX0, &ViewY0, &ViewX1, &ViewY1);

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.16f, 0.16f, 0.16f, 0.65f);
		IGraphics::CQuadItem Fade(ViewX0, ViewY0, ViewX1 - ViewX0, ViewY1 - ViewY0);
		Graphics()->QuadsDrawTL(&Fade, 1);
		Graphics()->QuadsEnd();

		const float Padding = TileSize * 0.5f;
		Graphics()->DrawRect(TopLeft.x - Padding, TopLeft.y - Padding, GridSize.x + Padding * 2.0f, GridSize.y + Padding * 2.0f, ColorRGBA(0.2f, 0.2f, 0.2f, 0.95f), IGraphics::CORNER_ALL, 12.0f * maximum(TileSize / 32.0f, 0.5f));
		Graphics()->DrawRect(TopLeft.x, TopLeft.y, GridSize.x, GridSize.y, ColorRGBA(0.24f, 0.24f, 0.24f, 0.98f), 0, 0.0f);

		CRenderMap *pRenderMap = RenderMap();
		if(pRenderMap)
		{
			const IGraphics::CTextureHandle PickerTexture = BrushPickerTexture(State.m_SelectedLayer);
			if(PickerTexture.IsValid())
			{
				Graphics()->TextureSet(PickerTexture);
				Graphics()->BlendNormal();
				const ColorRGBA TileTint = State.m_SelectedLayer == ELayerGroup::FRONT ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.7f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				for(int y = 0; y < BRUSH_PICKER_ROWS; ++y)
				{
					for(int x = 0; x < BRUSH_PICKER_COLS; ++x)
					{
						const int TileIndex = y * BRUSH_PICKER_COLS + x;
						const float TileX = TopLeft.x + x * TileSize;
						const float TileY = TopLeft.y + y * TileSize;
						pRenderMap->RenderTile(static_cast<int>(TileX), static_cast<int>(TileY), static_cast<unsigned char>(TileIndex), TileSize, TileTint);
					}
				}
			}
		}

		Graphics()->TextureClear();
		const ColorRGBA LineColor(1.0f, 1.0f, 1.0f, 0.06f);
		const float LineThickness = maximum(TileSize * 0.03f, 1.0f);
		for(int c = 1; c < BRUSH_PICKER_COLS; ++c)
		{
			const float LineX = TopLeft.x + c * TileSize - LineThickness * 0.5f;
			Graphics()->DrawRect(LineX, TopLeft.y, LineThickness, GridSize.y, LineColor, 0, 0.0f);
		}
		for(int r = 1; r < BRUSH_PICKER_ROWS; ++r)
		{
			const float LineY = TopLeft.y + r * TileSize - LineThickness * 0.5f;
			Graphics()->DrawRect(TopLeft.x, LineY, GridSize.x, LineThickness, LineColor, 0, 0.0f);
		}

		const auto RenderHighlight = [&](const ivec2 &Cell, const ColorRGBA &Color) {
			const float X = TopLeft.x + Cell.x * TileSize;
			const float Y = TopLeft.y + Cell.y * TileSize;
			Graphics()->DrawRect(X, Y, TileSize, TileSize, Color, 0, 0.0f);
		};
		if(State.m_BrushPickerHasHover)
		{
			RenderHighlight(State.m_BrushPickerHoverCell, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f));
		}
		if(State.m_BrushPickerSelecting)
		{
			const ivec2 Start = State.m_BrushPickerStartCell;
			const ivec2 End = State.m_BrushPickerCurrentCell;
			const int MinX = minimum(Start.x, End.x);
			const int MinY = minimum(Start.y, End.y);
			const int Width = abs(Start.x - End.x) + 1;
			const int Height = abs(Start.y - End.y) + 1;
			const float SelX = TopLeft.x + MinX * TileSize;
			const float SelY = TopLeft.y + MinY * TileSize;
			const float SelW = Width * TileSize;
			const float SelH = Height * TileSize;
			Graphics()->DrawRect(SelX, SelY, SelW, SelH, ColorRGBA(0.2f, 0.6f, 1.0f, 0.2f), 0, 0.0f);
			const float Border = maximum(TileSize * 0.06f, 1.0f);
			Graphics()->DrawRect(SelX, SelY, SelW, Border, ColorRGBA(0.7f, 0.85f, 1.0f, 0.9f), 0, 0.0f);
			Graphics()->DrawRect(SelX, SelY + SelH - Border, SelW, Border, ColorRGBA(0.7f, 0.85f, 1.0f, 0.9f), 0, 0.0f);
			Graphics()->DrawRect(SelX, SelY + Border, Border, SelH - 2.0f * Border, ColorRGBA(0.7f, 0.85f, 1.0f, 0.9f), 0, 0.0f);
			Graphics()->DrawRect(SelX + SelW - Border, SelY + Border, Border, SelH - 2.0f * Border, ColorRGBA(0.7f, 0.85f, 1.0f, 0.9f), 0, 0.0f);
		}

		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f));
		const float LabelFontSize = 18.0f * maximum(TileSize / 32.0f, 0.6f);
		const char *pLayerName = LayerDisplayName(State.m_SelectedLayer);
		const float LabelX = TopLeft.x;
		const float LabelY = TopLeft.y - Padding * 0.75f - LabelFontSize;
		TextRender()->Text(LabelX, LabelY, LabelFontSize, pLayerName);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
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
	ClearDrawState(State);
	State.m_ShiftHeld = false;
	State.m_AreaSelecting = false;
	State.m_AreaSelectionUseBrush = false;
	State.m_AreaSelectionLayer = ELayerGroup::GAME;
	State.m_AreaSelectStartWorld = vec2(0.0f, 0.0f);
	State.m_AreaSelectCurrentWorld = vec2(0.0f, 0.0f);
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
	EnsureDiffBaseline();

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
	InvalidateDiffBaseline();
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		DeactivateDummy(Dummy);
		m_aStates[Dummy] = SState();
	}
	m_DrawSegments.clear();
	m_DrawTexts.clear();
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
	if(State.m_AreaSelecting)
	{
		State.m_AreaSelectCurrentWorld = State.m_CursorWorld;
	}

	if(State.m_ToolPaletteDragging)
	{
		State.m_ToolPalettePos = State.m_CursorWorld - State.m_ToolPaletteDragOffset;
	}

	const bool PickerVisible = BrushPickerVisible(State);
	if(PickerVisible)
	{
		ivec2 Cell;
		if(BrushPickerCellFromWorld(State, State.m_CursorWorld, Cell))
		{
			State.m_BrushPickerHasHover = true;
			State.m_BrushPickerHoverCell = Cell;
			if(State.m_BrushPickerSelecting)
			{
				State.m_BrushPickerCurrentCell = Cell;
			}
		}
		else
		{
			State.m_BrushPickerHasHover = false;
			if(State.m_BrushPickerSelecting)
			{
				const float TileSize = BrushPickerTileSizeWorld();
				const vec2 TopLeft = BrushPickerTopLeft(TileSize);
				const vec2 Local = State.m_CursorWorld - TopLeft;
				const int CellX = std::clamp(static_cast<int>(floor(Local.x / TileSize)), 0, BRUSH_PICKER_COLS - 1);
				const int CellY = std::clamp(static_cast<int>(floor(Local.y / TileSize)), 0, BRUSH_PICKER_ROWS - 1);
				State.m_BrushPickerCurrentCell = ivec2(CellX, CellY);
			}
		}
	}
	else
	{
		State.m_BrushPickerSelecting = false;
		State.m_BrushPickerHasHover = false;
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

	if(State.m_DrawSampling)
	{
		HandleDrawSampling(State, Dummy, false);
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

	const auto CommitAreaSelection = [&]() {
		if(!State.m_AreaSelecting)
		{
			return;
		}
		ivec2 TopLeftTile;
		ivec2 Size;
		if(ComputeSelectionBounds(State.m_AreaSelectStartWorld, State.m_AreaSelectCurrentWorld, TopLeftTile, Size))
		{
			const bool UseBrush = State.m_AreaSelectionUseBrush && State.m_Brush.m_Active;
			if(UseBrush)
			{
				ApplyBrushTiledSelection(State, TopLeftTile, Size);
			}
			else
			{
				ApplyDestructiveAirSelection(State, TopLeftTile, Size, State.m_AreaSelectionLayer);
				if(State.m_AreaSelectionLayer == ELayerGroup::GAME && State.m_Destructive)
				{
					ApplyDestructiveAirSelection(State, TopLeftTile, Size, ELayerGroup::FRONT);
				}
			}
		}
		State.m_AreaSelecting = false;
		State.m_AreaSelectionUseBrush = false;
	};

	const auto BeginAreaSelection = [&]() {
		State.m_AreaSelecting = true;
		State.m_AreaSelectionUseBrush = State.m_Brush.m_Active;
		State.m_AreaSelectionLayer = State.m_SelectedLayer;
		State.m_AreaSelectStartWorld = State.m_CursorWorld;
		State.m_AreaSelectCurrentWorld = State.m_CursorWorld;
		State.m_BrushSelecting = false;
		State.m_BrushPainting = false;
		State.m_LastBrushApplyTile = ivec2(-1, -1);
	};

	const auto CommitBrushPickerSelection = [&]() {
		if(!State.m_BrushPickerSelecting)
		{
			return;
		}
		const ivec2 Start = State.m_BrushPickerStartCell;
		const ivec2 End = State.m_BrushPickerCurrentCell;
		ivec2 TopLeft(minimum(Start.x, End.x), minimum(Start.y, End.y));
		ivec2 Size(abs(Start.x - End.x) + 1, abs(Start.y - End.y) + 1);
		ApplyBrushPickerSelection(State, TopLeft, Size);
		State.m_BrushPickerSelecting = false;
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

	if(State.m_DrawTextEditing)
	{
		if((Event.m_Flags & IInput::FLAG_TEXT) && Event.m_aText[0])
		{
			const int InputLen = str_length(Event.m_aText);
			if(InputLen > 0)
			{
				std::string Filtered;
				Filtered.reserve(InputLen);
				for(int i = 0; i < InputLen; ++i)
				{
					const char c = Event.m_aText[i];
					if(c == '\r')
					{
						continue;
					}
					Filtered.push_back(c);
				}
				if(!Filtered.empty())
				{
					const size_t Remaining = MAX_DRAW_TEXT_LENGTH - State.m_DrawTextBuffer.size();
					if(Remaining >= Filtered.size())
					{
						State.m_DrawTextBuffer.append(Filtered);
					}
				}
			}
			return true;
		}
		if(Event.m_Key == KEY_LSHIFT || Event.m_Key == KEY_RSHIFT)
		{
			if(Press)
			{
				State.m_ShiftHeld = true;
			}
			else if(Release)
			{
				State.m_ShiftHeld = false;
			}
			return true;
		}
		if(Press)
		{
			if(Event.m_Key == KEY_ESCAPE)
			{
				CancelDrawText(State);
				return true;
			}
			if(Event.m_Key == KEY_BACKSPACE || Event.m_Key == KEY_DELETE)
			{
				if(!State.m_DrawTextBuffer.empty())
				{
					State.m_DrawTextBuffer.pop_back();
				}
				return true;
			}
			if(Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER)
			{
				if(State.m_ShiftHeld)
				{
					if(State.m_DrawTextBuffer.size() < MAX_DRAW_TEXT_LENGTH)
					{
						State.m_DrawTextBuffer.push_back('\n');
					}
				}
				else
				{
					SubmitDrawText(State, Dummy);
				}
				return true;
			}
			return true;
		}
		if(Release)
		{
			return true;
		}
		return true;
	}

	if(Event.m_Key == KEY_SPACE)
	{
		if(Press && !State.m_SpaceHeld)
		{
			State.m_SpaceHeld = true;
			State.m_AreaSelecting = false;
			State.m_BrushSelecting = false;
			State.m_BrushPainting = false;
			State.m_BrushPickerSelecting = false;
		}
		else if(Release && State.m_SpaceHeld)
		{
			if(State.m_BrushPickerSelecting)
			{
				CommitBrushPickerSelection();
			}
			State.m_SpaceHeld = false;
			State.m_BrushPickerSelecting = false;
			State.m_BrushPickerHasHover = false;
		}
		return false;
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

	if(Press)
	{
		if(Event.m_Key == KEY_R && RotateBrush(State, false))
			return true;
		if(Event.m_Key == KEY_T && RotateBrush(State, true))
			return true;
		if(Event.m_Key == KEY_N && MirrorBrush(State, true))
			return true;
		if(Event.m_Key == KEY_M && MirrorBrush(State, false))
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
		if(BrushPickerVisible(State))
			return true;
		if(Press)
		{
			State.m_AreaSelecting = false;
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
				if(State.m_SelectedPrimaryTool == PRIMARY_TOOL_DRAW)
				{
					FinishTeleInputEditing(State);
					EndDrawStroke(State);
					if(State.m_DrawTextEditing)
					{
						CancelDrawText(State);
					}
					BeginDrawText(State, State.m_CursorWorld);
				}
				else
				{
					State.m_BrushSelecting = false;
					ClearBrush(State);
				}
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

		const bool PickerVisibleNow = BrushPickerVisible(State);
		if(PickerVisibleNow)
		{
			if(Press)
			{
				ivec2 Cell;
				if(BrushPickerCellFromWorld(State, State.m_CursorWorld, Cell))
				{
					State.m_BrushPickerSelecting = true;
					State.m_BrushPickerStartCell = Cell;
					State.m_BrushPickerCurrentCell = Cell;
				}
			}
			else if(Release)
			{
				CommitBrushPickerSelection();
			}
			return true;
		}

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
		const vec2 ShowDiffPos = ToolPaletteShowDiffButtonPos(State.m_ToolPalettePos);
		const vec2 ShowDiffSize = ToolPaletteShowDiffButtonSize();
		const bool CursorInDestructiveToggle = State.m_ToolPaletteActive && PointInRect(State.m_CursorWorld, TogglePos, ToggleSize);
		const bool CursorInShowDiffToggle = State.m_ToolPaletteActive && PointInRect(State.m_CursorWorld, ShowDiffPos, ShowDiffSize);
		const bool CursorOverToolPaletteUi = CursorInPaletteBody || CursorInDropdownOptions || CursorInDestructiveToggle || CursorInShowDiffToggle;

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
				ClearDrawState(State);
				Consumed = true;
			}
		}
		if(!Consumed && State.m_ToolPaletteActive && Press)
		{
			const vec2 SecondaryButtonPos = ToolPaletteSecondaryButtonPos(State.m_ToolPalettePos);
			const vec2 SecondaryButtonSize = ToolPalettePrimaryButtonSize();
			if(PointInRect(State.m_CursorWorld, SecondaryButtonPos, SecondaryButtonSize))
			{
				State.m_SelectedPrimaryTool = PRIMARY_TOOL_DRAW;
				ClearBrush(State);
				ClearDrawState(State);
				Consumed = true;
			}
		}
		if(!Consumed && State.m_ToolPaletteActive && Press && CursorInDestructiveToggle)
		{
			State.m_Destructive = !State.m_Destructive;
			Consumed = true;
		}
		if(!Consumed && State.m_ToolPaletteActive && Press && CursorInShowDiffToggle)
		{
			State.m_ShowDiff = !State.m_ShowDiff;
			if(State.m_ShowDiff)
			{
				EnsureDiffBaseline();
			}
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
		if(State.m_AreaSelecting)
		{
			if(Release)
			{
				CommitAreaSelection();
			}
			return true;
		}

		if(!State.m_Active)
			return State.m_CtrlHeld;

		const bool PaintbrushEnabled = State.m_SelectedPrimaryTool == PRIMARY_TOOL_PAINTBRUSH && !State.m_CtrlHeld && !CursorOverToolPaletteUi && !State.m_DrawTextEditing;
		const bool DrawEnabled = State.m_SelectedPrimaryTool == PRIMARY_TOOL_DRAW && !State.m_CtrlHeld && !CursorOverToolPaletteUi && !State.m_DrawTextEditing;
		if(DrawEnabled)
		{
			if(Press)
			{
				BeginDrawStroke(State, Dummy);
				return true;
			}
			if(Release)
			{
				EndDrawStroke(State);
				return true;
			}
			if(State.m_DrawSampling)
			{
				return true;
			}
		}
		if(PaintbrushEnabled && State.m_ShiftHeld && Press)
		{
			BeginAreaSelection();
			return true;
		}

		if(PaintbrushEnabled && State.m_Brush.m_Active && !State.m_ShiftHeld)
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

		if(PaintbrushEnabled && !State.m_Brush.m_Active && !State.m_ShiftHeld)
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

	if(Event.m_Key == KEY_LSHIFT || Event.m_Key == KEY_RSHIFT)
	{
		if(Press)
			State.m_ShiftHeld = true;
		else if(Release)
			State.m_ShiftHeld = false;
		return false;
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

		const vec2 DrawButtonPos = ToolPaletteSecondaryButtonPos(State.m_ToolPalettePos);
		const bool SelectedDraw = State.m_SelectedPrimaryTool == PRIMARY_TOOL_DRAW;
		const ColorRGBA DrawButtonColor = SelectedDraw ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.95f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.85f);
		Graphics()->DrawRect(DrawButtonPos.x, DrawButtonPos.y, PrimaryButtonSize.x, PrimaryButtonSize.y, DrawButtonColor, IGraphics::CORNER_ALL, TOOL_BUTTON_ROUNDING);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const float DrawFontSize = 14.0f;
		const float DrawTextWidth = TextRender()->TextWidth(DrawFontSize, "Draw");
		const float DrawTextX = DrawButtonPos.x + (PrimaryButtonSize.x - DrawTextWidth) * 0.5f;
		const float DrawTextY = DrawButtonPos.y + (PrimaryButtonSize.y - DrawFontSize) * 0.5f;
		TextRender()->Text(DrawTextX, DrawTextY, DrawFontSize, "Draw");

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

		const vec2 ShowDiffPos = ToolPaletteShowDiffButtonPos(State.m_ToolPalettePos);
		const vec2 ShowDiffSize = ToolPaletteShowDiffButtonSize();
		const bool ShowDiffHover = PointInRect(State.m_CursorWorld, ShowDiffPos, ShowDiffSize);
		ColorRGBA ShowDiffColor;
		if(State.m_ShowDiff)
		{
			ShowDiffColor = ShowDiffHover ? ColorRGBA(0.3f, 0.75f, 1.0f, 0.95f) : ColorRGBA(0.22f, 0.62f, 0.95f, 0.9f);
		}
		else
		{
			ShowDiffColor = ShowDiffHover ? ColorRGBA(0.35f, 0.35f, 0.35f, 0.9f) : ColorRGBA(0.22f, 0.22f, 0.22f, 0.85f);
		}
		Graphics()->DrawRect(ShowDiffPos.x, ShowDiffPos.y, ShowDiffSize.x, ShowDiffSize.y, ShowDiffColor, IGraphics::CORNER_ALL, TOOL_BUTTON_ROUNDING);
		const float ShowDiffFontSize = 13.0f;
		const float ShowDiffTextY = ShowDiffPos.y + (ShowDiffSize.y - ShowDiffFontSize) * 0.5f;
		TextRender()->Text(ShowDiffPos.x + 10.0f, ShowDiffTextY, ShowDiffFontSize, "Show Diff");
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

	PruneExpiredDrawSegments();
	PruneExpiredDrawTexts();
	RenderDrawSegments();
	if(State.m_ShowDiff)
	{
		EnsureDiffBaseline();
		RenderDiffOverlay(State);
	}
	RenderDrawTexts();
	RenderBrushOverlay(State);
	RenderDrawTextPreview(State);

	if(State.m_ToolPaletteActive)
	{
		RenderToolPalette();
		if(State.m_SelectedLayer == ELayerGroup::TELE)
			RenderTeleMenu();
		RenderLayerDropdownOptions();
	}

	RenderBrushPicker(State);
    
	if(State.m_Active)
	{
		GameClient()->RenderTools()->RenderCursor(State.m_CursorWorld, 48.0f);
	}

	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CEditorSpec::OnRemoteDrawSegments(const CNetMsg_Sv_EditorSpecDrawSegments *pMsg)
{
	if(!pMsg)
	{
		return;
	}
	const int Count = std::clamp(pMsg->m_Count, 0, MAX_DRAW_SEGMENTS_PER_PACKET);
	for(int i = 0; i < Count; ++i)
	{
		const vec2 Start((float)pMsg->m_aStartX[i], (float)pMsg->m_aStartY[i]);
		const vec2 End((float)pMsg->m_aEndX[i], (float)pMsg->m_aEndY[i]);
		if(distance(Start, End) < 0.5f)
		{
			continue;
		}
		const ColorRGBA Color(
			pMsg->m_aColorR[i] / 255.0f,
			pMsg->m_aColorG[i] / 255.0f,
			pMsg->m_aColorB[i] / 255.0f,
			0.95f);
		AddDrawSegment(Start, End, Color, pMsg->m_ClientId);
	}
}

void CEditorSpec::OnRemoteDrawText(const CNetMsg_Sv_EditorSpecDrawText *pMsg)
{
	if(!pMsg)
	{
		return;
	}
	const ColorRGBA Color(
		pMsg->m_ColorR / 255.0f,
		pMsg->m_ColorG / 255.0f,
		pMsg->m_ColorB / 255.0f,
		0.95f);
	AddDrawText(vec2(pMsg->m_PosX, pMsg->m_PosY), pMsg->m_pPlayerName, pMsg->m_pText, Color, pMsg->m_ClientId);
}

void CEditorSpec::BeginDrawStroke(SState &State, int Dummy)
{
	State.m_DrawSampling = true;
	State.m_DrawHasSample = false;
	State.m_DrawNextSampleTick = Client()->GameTick(Dummy);
	HandleDrawSampling(State, Dummy, true);
}

void CEditorSpec::EndDrawStroke(SState &State)
{
	State.m_DrawSampling = false;
	State.m_DrawHasSample = false;
}

void CEditorSpec::HandleDrawSampling(SState &State, int Dummy, bool ForceSample)
{
	if(!State.m_DrawSampling)
	{
		return;
	}
	const int CurrentTick = Client()->GameTick(Dummy);
	if(!ForceSample && CurrentTick < State.m_DrawNextSampleTick)
	{
		return;
	}
	const vec2 CurrentPos = State.m_CursorWorld;
	if(State.m_DrawHasSample)
	{
		const vec2 PrevPos = State.m_DrawLastSampleWorld;
		if(distance(PrevPos, CurrentPos) > 0.5f)
		{
			SubmitDrawSegment(PrevPos, CurrentPos, Dummy);
		}
	}
	State.m_DrawLastSampleWorld = CurrentPos;
	State.m_DrawHasSample = true;
	State.m_DrawNextSampleTick = CurrentTick + DRAW_SAMPLE_TICK_STEP;
}

void CEditorSpec::SubmitDrawSegment(const vec2 &Start, const vec2 &End, int Dummy)
{
	if(distance(Start, End) < 0.5f)
	{
		return;
	}
	const ColorRGBA Color = DrawColorForDummy(Dummy);
	if(GameClient()->SendEditorSpecDrawSegment(Start, End, Color, Dummy))
	{
		const int ClientId = GameClient()->ClientIdForDummy(Dummy);
		AddDrawSegment(Start, End, Color, ClientId);
	}
}

void CEditorSpec::AddDrawSegment(const vec2 &Start, const vec2 &End, const ColorRGBA &Color, int ClientId)
{
	if(distance(Start, End) < 0.5f)
	{
		return;
	}
	SDrawSegment Segment;
	Segment.m_Start = Start;
	Segment.m_End = End;
	Segment.m_Color = Color;
	const int Lifetime = Client()->GameTickSpeed() * DRAW_SEGMENT_LIFETIME_SECONDS;
	Segment.m_ExpireTick = Client()->GameTick(g_Config.m_ClDummy) + Lifetime;
	Segment.m_ClientId = ClientId;
	m_DrawSegments.push_back(Segment);
	if(m_DrawSegments.size() > MAX_STORED_DRAW_SEGMENTS)
	{
		const size_t Overflow = m_DrawSegments.size() - MAX_STORED_DRAW_SEGMENTS;
		m_DrawSegments.erase(m_DrawSegments.begin(), m_DrawSegments.begin() + Overflow);
	}
}

void CEditorSpec::BeginDrawText(SState &State, const vec2 &Anchor)
{
	State.m_DrawSampling = false;
	State.m_DrawHasSample = false;
	State.m_DrawTextEditing = true;
	State.m_DrawTextAnchor = Anchor;
	State.m_DrawTextBuffer.clear();
	Input()->StartTextInput();
}

void CEditorSpec::CancelDrawText(SState &State)
{
	if(State.m_DrawTextEditing)
	{
		Input()->StopTextInput();
	}
	State.m_DrawTextEditing = false;
	State.m_DrawTextBuffer.clear();
}

void CEditorSpec::SubmitDrawText(SState &State, int Dummy)
{
	std::string Trimmed = State.m_DrawTextBuffer;
	while(!Trimmed.empty() && (Trimmed.back() == '\n' || Trimmed.back() == '\r' || Trimmed.back() == ' '))
	{
		Trimmed.pop_back();
	}
	if(Trimmed.empty())
	{
		CancelDrawText(State);
		return;
	}
	const ColorRGBA Color = DrawColorForDummy(Dummy);
	if(GameClient()->SendEditorSpecDrawText(State.m_DrawTextAnchor, Trimmed.c_str(), Color, Dummy))
	{
		const char *pName = LocalPlayerName(Dummy);
		AddDrawText(State.m_DrawTextAnchor, pName ? pName : "", Trimmed.c_str(), Color, GameClient()->ClientIdForDummy(Dummy));
	}
	CancelDrawText(State);
}

void CEditorSpec::AddDrawText(const vec2 &Pos, const char *pPlayerName, const char *pText, const ColorRGBA &Color, int ClientId)
{
	if(!pText || !pText[0])
	{
		return;
	}
	SDrawText Entry;
	Entry.m_Pos = Pos;
	Entry.m_Text = pText;
	if(Entry.m_Text.size() > MAX_DRAW_TEXT_LENGTH)
	{
		Entry.m_Text.resize(MAX_DRAW_TEXT_LENGTH);
	}
	Entry.m_PlayerName = pPlayerName ? pPlayerName : "";
	Entry.m_Color = Color;
	Entry.m_ClientId = ClientId;
	Entry.m_ExpireTick = Client()->GameTick(g_Config.m_ClDummy) + Client()->GameTickSpeed() * DRAW_SEGMENT_LIFETIME_SECONDS;
	m_DrawTexts.push_back(Entry);
	if(m_DrawTexts.size() > MAX_STORED_DRAW_TEXTS)
	{
		const size_t Overflow = m_DrawTexts.size() - MAX_STORED_DRAW_TEXTS;
		m_DrawTexts.erase(m_DrawTexts.begin(), m_DrawTexts.begin() + Overflow);
	}
}

void CEditorSpec::RenderDrawSegments()
{
	if(m_DrawSegments.empty())
	{
		return;
	}
	std::vector<IGraphics::CFreeformItem> vItems;
	vItems.reserve(m_DrawSegments.size());
	for(const auto &Segment : m_DrawSegments)
	{
		const vec2 Dir = Segment.m_End - Segment.m_Start;
		if(length(Dir) < 0.5f)
		{
			continue;
		}
		const vec2 Normal = normalize(vec2(Dir.y, -Dir.x)) * DRAW_LINE_HALF_WIDTH;
		const vec2 Pos0 = Segment.m_End + Normal;
		const vec2 Pos1 = Segment.m_End - Normal;
		const vec2 Pos2 = Segment.m_Start - Normal;
		const vec2 Pos3 = Segment.m_Start + Normal;
		vItems.emplace_back(Pos0.x, Pos0.y, Pos1.x, Pos1.y, Pos3.x, Pos3.y, Pos2.x, Pos2.y);
	}
	if(vItems.empty())
	{
		return;
	}
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(size_t i = 0, idx = 0; i < m_DrawSegments.size(); ++i)
	{
		const auto &Segment = m_DrawSegments[i];
		const vec2 Dir = Segment.m_End - Segment.m_Start;
		if(length(Dir) < 0.5f)
		{
			continue;
		}
		Graphics()->SetColor(Segment.m_Color);
		Graphics()->QuadsDrawFreeform(&vItems[idx], 1);
		++idx;
	}
	Graphics()->QuadsEnd();
}

void CEditorSpec::RenderDrawTexts()
{
	if(m_DrawTexts.empty())
	{
		return;
	}
	float WorldX0, WorldY0, WorldX1, WorldY1;
	Graphics()->GetScreen(&WorldX0, &WorldY0, &WorldX1, &WorldY1);
	const float WorldWidth = WorldX1 - WorldX0;
	const float WorldHeight = WorldY1 - WorldY0;
	if(WorldWidth <= 0.0f || WorldHeight <= 0.0f)
	{
		return;
	}
	const float ScreenWidth = Graphics()->ScreenWidth();
	const float ScreenHeight = Graphics()->ScreenHeight();
	const auto WorldToScreen = [&](const vec2 &WorldPos) {
		const float U = (WorldPos.x - WorldX0) / WorldWidth;
		const float V = (WorldPos.y - WorldY0) / WorldHeight;
		return vec2(U * ScreenWidth, V * ScreenHeight);
	};
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);
	const float NameFontSize = 24.0f;
	const float TextFontSize = 36.0f;
	const float LineSpacing = TextFontSize + 4.0f;
	for(const auto &Entry : m_DrawTexts)
	{
		const vec2 ScreenAnchor = WorldToScreen(Entry.m_Pos);
		TextRender()->TextColor(Entry.m_Color);
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f));
		const float NameY = ScreenAnchor.y - (NameFontSize + 4.0f);
		TextRender()->Text(ScreenAnchor.x, NameY, NameFontSize, Entry.m_PlayerName.c_str());
		int LineIndex = 0;
		size_t Start = 0;
		while(true)
		{
			size_t End = Entry.m_Text.find('\n', Start);
			std::string Line;
			if(End == std::string::npos)
			{
				Line = Entry.m_Text.substr(Start);
			}
			else
			{
				Line = Entry.m_Text.substr(Start, End - Start);
			}
			const float LineY = ScreenAnchor.y + LineSpacing * LineIndex;
			if(!Line.empty())
			{
				TextRender()->Text(ScreenAnchor.x, LineY, TextFontSize, Line.c_str());
			}
			if(End == std::string::npos)
			{
				break;
			}
			Start = End + 1;
			++LineIndex;
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	Graphics()->MapScreen(WorldX0, WorldY0, WorldX1, WorldY1);
}

void CEditorSpec::RenderDiffOverlay(const SState &State) const
{
	if(!State.m_ShowDiff || !m_DiffBaselineValid)
	{
		return;
	}
	const ELayerGroup Group = State.m_SelectedLayer;
	const auto &Snapshot = m_aDiffBaselines[static_cast<int>(Group)];
	if(!Snapshot.m_Valid || Snapshot.m_Tiles.empty())
	{
		return;
	}
	CLayers *pLayers = GameClient()->Layers();
	if(!pLayers)
	{
		return;
	}
	IMap *pMap = pLayers->Map();
	if(!pMap)
	{
		return;
	}
	CMapItemLayerTilemap *pTilemap = pLayers->GetTilemapForLayer(LayerIndexForGroup(Group));
	if(!pTilemap)
	{
		return;
	}
	const int MapWidth = pTilemap->m_Width;
	const int MapHeight = pTilemap->m_Height;
	if(MapWidth <= 0 || MapHeight <= 0)
	{
		return;
	}
	CTile *pTiles = LayerTileData(pMap, pTilemap, LayerIndexForGroup(Group));
	if(!pTiles)
	{
		return;
	}
	const CTeleTile *pTeleTiles = nullptr;
	if(Group == ELayerGroup::TELE)
	{
		pTeleTiles = TeleLayerData(pMap, pTilemap);
	}
	float ViewX0, ViewY0, ViewX1, ViewY1;
	Graphics()->GetScreen(&ViewX0, &ViewY0, &ViewX1, &ViewY1);
	const float TileSize = 32.0f;
	int TileX0 = maximum(0, (int)floorf(ViewX0 / TileSize) - 1);
	int TileY0 = maximum(0, (int)floorf(ViewY0 / TileSize) - 1);
	int TileX1 = minimum(MapWidth - 1, (int)ceilf(ViewX1 / TileSize) + 1);
	int TileY1 = minimum(MapHeight - 1, (int)ceilf(ViewY1 / TileSize) + 1);
	if(TileX1 < TileX0 || TileY1 < TileY0)
	{
		return;
	}
	const size_t MapTileCount = (size_t)MapWidth * MapHeight;
	const ColorRGBA AddColor(0.2f, 0.85f, 0.3f, 0.15f);
	const ColorRGBA RemoveColor(0.95f, 0.3f, 0.3f, 0.15f);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int y = TileY0; y <= TileY1; ++y)
	{
		if(y < 0 || y >= Snapshot.m_Height)
		{
			continue;
		}
		for(int x = TileX0; x <= TileX1; ++x)
		{
			if(x < 0 || x >= Snapshot.m_Width)
			{
				continue;
			}
			const size_t BaseIndex = (size_t)y * Snapshot.m_Width + x;
			const size_t CurrentIndex = (size_t)y * MapWidth + x;
			if(BaseIndex >= Snapshot.m_Tiles.size() || CurrentIndex >= MapTileCount)
			{
				continue;
			}
			const STileSample &BaseTile = Snapshot.m_Tiles[BaseIndex];
			STileSample CurrentSample{};
			CurrentSample.m_Index = pTiles[CurrentIndex].m_Index;
			CurrentSample.m_Flags = pTiles[CurrentIndex].m_Flags;
			bool Different = CurrentSample.m_Index != BaseTile.m_Index || CurrentSample.m_Flags != BaseTile.m_Flags;
			STeleSample BaseTele{};
			STeleSample CurrentTele{};
			if(Group == ELayerGroup::TELE)
			{
				if(BaseIndex < Snapshot.m_Tele.size())
				{
					BaseTele = Snapshot.m_Tele[BaseIndex];
				}
				if(pTeleTiles && CurrentIndex < MapTileCount)
				{
					const CTeleTile &TeleTile = pTeleTiles[CurrentIndex];
					CurrentTele.m_Number = TeleTile.m_Number;
					CurrentTele.m_Type = TeleTile.m_Type;
				}
				Different = Different || CurrentTele.m_Number != BaseTele.m_Number || CurrentTele.m_Type != BaseTele.m_Type;
			}
			if(!Different)
			{
				continue;
			}
			const bool CurrentHasContent = TileHasContent(Group, CurrentSample, CurrentTele);
			const bool BaseHasContent = TileHasContent(Group, BaseTile, BaseTele);
			if(!CurrentHasContent && !BaseHasContent)
			{
				continue;
			}
			const ColorRGBA &Color = CurrentHasContent ? AddColor : RemoveColor;
			Graphics()->SetColor(Color);
			const float QuadX = x * TileSize;
			const float QuadY = y * TileSize;
			IGraphics::CQuadItem Quad(QuadX, QuadY, TileSize, TileSize);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
	}
	Graphics()->QuadsEnd();
}

void CEditorSpec::RenderDrawTextPreview(const SState &State) const
{
	if(!State.m_DrawTextEditing)
	{
		return;
	}
	float WorldX0, WorldY0, WorldX1, WorldY1;
	Graphics()->GetScreen(&WorldX0, &WorldY0, &WorldX1, &WorldY1);
	const float WorldWidth = WorldX1 - WorldX0;
	const float WorldHeight = WorldY1 - WorldY0;
	if(WorldWidth <= 0.0f || WorldHeight <= 0.0f)
	{
		return;
	}
	const float ScreenWidth = Graphics()->ScreenWidth();
	const float ScreenHeight = Graphics()->ScreenHeight();
	const auto WorldToScreen = [&](const vec2 &WorldPos) {
		const float U = (WorldPos.x - WorldX0) / WorldWidth;
		const float V = (WorldPos.y - WorldY0) / WorldHeight;
		return vec2(U * ScreenWidth, V * ScreenHeight);
	};
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);
	const float NameFontSize = 24.0f;
	const float TextFontSize = 36.0f;
	const float LineSpacing = TextFontSize + 4.0f;
	const int Dummy = CurrentDummy();
	const ColorRGBA TextColor = DrawColorForDummy(Dummy);
	TextRender()->TextColor(TextColor);
	TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f));
	const char *pName = LocalPlayerName(Dummy);
	const vec2 ScreenAnchor = WorldToScreen(State.m_DrawTextAnchor);
	const float NameY = ScreenAnchor.y - (NameFontSize + 4.0f);
	TextRender()->Text(ScreenAnchor.x, NameY, NameFontSize, pName ? pName : "");
	int LineIndex = 0;
	size_t Start = 0;
	const std::string &Buffer = State.m_DrawTextBuffer;
	if(Buffer.empty())
	{
		TextRender()->Text(ScreenAnchor.x, ScreenAnchor.y, TextFontSize, "");
	}
	else
	{
		while(true)
		{
			size_t End = Buffer.find('\n', Start);
			std::string Line;
			if(End == std::string::npos)
			{
				Line = Buffer.substr(Start);
			}
			else
			{
				Line = Buffer.substr(Start, End - Start);
			}
			const float LineY = ScreenAnchor.y + LineSpacing * LineIndex;
			if(!Line.empty())
			{
				TextRender()->Text(ScreenAnchor.x, LineY, TextFontSize, Line.c_str());
			}
			if(End == std::string::npos)
			{
				break;
			}
			Start = End + 1;
			++LineIndex;
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	Graphics()->MapScreen(WorldX0, WorldY0, WorldX1, WorldY1);
}

void CEditorSpec::EnsureDiffBaseline()
{
	if(m_DiffBaselineValid)
	{
		return;
	}
	CaptureDiffBaseline();
}

void CEditorSpec::CaptureDiffBaseline()
{
	CLayers *pLayers = GameClient()->Layers();
	IMap *pMap = pLayers ? pLayers->Map() : nullptr;
	if(!pLayers || !pMap)
	{
		InvalidateDiffBaseline();
		return;
	}
	for(int LayerIdx = 0; LayerIdx < static_cast<int>(ELayerGroup::COUNT); ++LayerIdx)
	{
		auto &Snapshot = m_aDiffBaselines[LayerIdx];
		Snapshot.Clear();
		const ELayerGroup Group = static_cast<ELayerGroup>(LayerIdx);
		CMapItemLayerTilemap *pTilemap = pLayers->GetTilemapForLayer(LayerIndexForGroup(Group));
		if(!pTilemap)
		{
			continue;
		}
		const size_t TileCount = (size_t)pTilemap->m_Width * pTilemap->m_Height;
		if(TileCount == 0)
		{
			continue;
		}
		CTile *pTiles = LayerTileData(pMap, pTilemap, LayerIndexForGroup(Group));
		if(!pTiles)
		{
			continue;
		}
		Snapshot.m_Width = pTilemap->m_Width;
		Snapshot.m_Height = pTilemap->m_Height;
		Snapshot.m_Tiles.resize(TileCount);
		for(size_t i = 0; i < TileCount; ++i)
		{
			Snapshot.m_Tiles[i].m_Index = pTiles[i].m_Index;
			Snapshot.m_Tiles[i].m_Flags = pTiles[i].m_Flags;
		}
		if(Group == ELayerGroup::TELE)
		{
			Snapshot.m_Tele.resize(TileCount);
			CTeleTile *pTeleTiles = TeleLayerData(pMap, pTilemap);
			if(pTeleTiles)
			{
				for(size_t i = 0; i < TileCount; ++i)
				{
					Snapshot.m_Tele[i].m_Number = pTeleTiles[i].m_Number;
					Snapshot.m_Tele[i].m_Type = pTeleTiles[i].m_Type;
				}
			}
			else
			{
				for(auto &Cell : Snapshot.m_Tele)
				{
					Cell.m_Number = 0;
					Cell.m_Type = 0;
				}
			}
		}
		Snapshot.m_Valid = true;
	}
	m_DiffBaselineValid = true;
}

void CEditorSpec::InvalidateDiffBaseline()
{
	m_DiffBaselineValid = false;
	for(auto &Snapshot : m_aDiffBaselines)
	{
		Snapshot.Clear();
	}
}

void CEditorSpec::PruneExpiredDrawSegments()
{
	if(m_DrawSegments.empty())
	{
		return;
	}
	const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	m_DrawSegments.erase(
		std::remove_if(m_DrawSegments.begin(), m_DrawSegments.end(), [CurrentTick](const SDrawSegment &Segment) {
			return Segment.m_ExpireTick <= CurrentTick;
		}),
		m_DrawSegments.end());
}

void CEditorSpec::PruneExpiredDrawTexts()
{
	if(m_DrawTexts.empty())
	{
		return;
	}
	const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	m_DrawTexts.erase(
		std::remove_if(m_DrawTexts.begin(), m_DrawTexts.end(), [CurrentTick](const SDrawText &Entry) {
			return Entry.m_ExpireTick <= CurrentTick;
		}),
		m_DrawTexts.end());
}

void CEditorSpec::ClearDrawState(SState &State)
{
	State.m_DrawSampling = false;
	State.m_DrawHasSample = false;
	State.m_DrawLastSampleWorld = vec2(0.0f, 0.0f);
	State.m_DrawNextSampleTick = 0;
	CancelDrawText(State);
}

const char *CEditorSpec::LocalPlayerName(int Dummy) const
{
	const int ClientId = GameClient()->ClientIdForDummy(Dummy);
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		return GameClient()->m_aClients[ClientId].m_aName;
	}
	return "";
}

ColorRGBA CEditorSpec::DrawColorForDummy(int Dummy) const
{
	const int ClientId = GameClient()->ClientIdForDummy(Dummy);
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		ColorRGBA Color = ClientData.m_RenderInfo.m_ColorBody;
		if(Color.a <= 0.0f)
		{
			Color.a = 1.0f;
		}
		return Color;
	}
	return DRAW_DEFAULT_COLOR;
}
