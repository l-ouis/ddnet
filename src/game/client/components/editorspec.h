/* (c) DDNet contributors. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_COMPONENTS_EDITORSPEC_H
#define GAME_CLIENT_COMPONENTS_EDITORSPEC_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/input.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/mapitems.h>

#include <array>
#include <string>
#include <vector>

struct CNetMsg_Sv_EditorSpecDrawSegments;
struct CNetMsg_Sv_EditorSpecDrawText;

class CEditorSpec : public CComponent
{
public:
	enum class ELayerGroup
	{
		GAME = 0,
		FRONT,
		TELE,
		COUNT
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnNewSnapshot() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRender() override;
	void OnReset() override;
	void OnRemoteDrawSegments(const CNetMsg_Sv_EditorSpecDrawSegments *pMsg);
	void OnRemoteDrawText(const CNetMsg_Sv_EditorSpecDrawText *pMsg);

	bool IsActive() const;
	void Deactivate();

private:
	static constexpr int TELE_INPUT_BUFFER_SIZE = 6;
	static constexpr int TELE_NUMBER_DEFAULT = 1;

	struct STileSample
	{
		int m_Index = TILE_AIR;
		int m_Flags = 0;
	};

	struct STeleSample
	{
		int m_Type = 0;
		int m_Number = 0;
	};

	struct SBrushLayer
	{
		ELayerGroup m_Group = ELayerGroup::GAME;
		int m_Width = 0;
		int m_Height = 0;
		std::vector<STileSample> m_Tiles;
		std::vector<STeleSample> m_Tele;

		void Clear()
		{
			m_Width = 0;
			m_Height = 0;
			m_Tiles.clear();
			m_Tele.clear();
		}

		bool Empty() const { return m_Tiles.empty(); }
	};

	struct SBrush
	{
		bool m_Active = false;
		ivec2 m_Size = ivec2(0, 0);
		std::array<SBrushLayer, static_cast<int>(ELayerGroup::COUNT)> m_aLayers;

		void Clear()
		{
			m_Active = false;
			m_Size = ivec2(0, 0);
			for(auto &Layer : m_aLayers)
			{
				Layer.Clear();
			}
		}
	};

	struct SState
	{
		bool m_Active = false;
		bool m_MiddleMousePan = false;
		bool m_LeftMouseHeld = false;
		bool m_CtrlHeld = false;
		bool m_ShiftHeld = false;
		vec2 m_CursorWorld = vec2(0.0f, 0.0f);
		ivec2 m_LastSentCursor = ivec2(0, 0);
		bool m_ToolPaletteActive = false;
		vec2 m_ToolPalettePos = vec2(0.0f, 0.0f);
		bool m_ToolPaletteDragging = false;
		vec2 m_ToolPaletteDragOffset = vec2(0.0f, 0.0f);
		int m_SelectedPrimaryTool = 0;
		bool m_Destructive = true;
		bool m_ShowDiff = false;
		ELayerGroup m_SelectedLayer = ELayerGroup::GAME;
		bool m_LayerDropdownOpen = false;
		int m_SelectedTeleNumber = TELE_NUMBER_DEFAULT;
		bool m_TeleInputEditing = false;
		bool m_TeleInputOverwrite = false;
		int m_TeleInputLength = 0;
		char m_aTeleInput[TELE_INPUT_BUFFER_SIZE] = {};
		bool m_BrushSelecting = false;
		vec2 m_BrushSelectStartWorld = vec2(0.0f, 0.0f);
		vec2 m_BrushSelectCurrentWorld = vec2(0.0f, 0.0f);
		bool m_AreaSelecting = false;
		bool m_AreaSelectionUseBrush = false;
		ELayerGroup m_AreaSelectionLayer = ELayerGroup::GAME;
		vec2 m_AreaSelectStartWorld = vec2(0.0f, 0.0f);
		vec2 m_AreaSelectCurrentWorld = vec2(0.0f, 0.0f);
		bool m_BrushPreviewValid = false;
		ivec2 m_BrushPreviewTile = ivec2(0, 0);
		bool m_BrushPainting = false;
		ivec2 m_LastBrushApplyTile = ivec2(-1, -1);
		bool m_DrawSampling = false;
		bool m_DrawHasSample = false;
		vec2 m_DrawLastSampleWorld = vec2(0.0f, 0.0f);
		int m_DrawNextSampleTick = 0;
		bool m_DrawTextEditing = false;
		vec2 m_DrawTextAnchor = vec2(0.0f, 0.0f);
		std::string m_DrawTextBuffer;
		SBrush m_Brush;
	};

	SState m_aStates[NUM_DUMMIES];

	static void ConEditorSpec(IConsole::IResult *pResult, void *pUserData);

	bool CanActivate() const;
	void Activate();
	float CursorMoveFactor(IInput::ECursorType CursorType) const;
	void MaybeSendCursorUpdate();
	bool IsPanning(const SState &State) const { return State.m_MiddleMousePan || (State.m_CtrlHeld && State.m_LeftMouseHeld); }
	void DeactivateDummy(int Dummy, bool NotifyServer = true);
	int CurrentDummy() const;
	SState &StateForDummy(int Dummy);
	const SState &StateForDummy(int Dummy) const;
	void ResetTeleInputBuffer(SState &State);
	void FinishTeleInputEditing(SState &State);
	void AdjustTeleNumber(SState &State, int Delta);
	bool WorldToTile(const vec2 &WorldPos, ivec2 &OutTile) const;
	bool ComputeSelectionBounds(const vec2 &StartWorld, const vec2 &EndWorld, ivec2 &OutTopLeft, ivec2 &OutSize) const;
	bool CaptureBrushFromSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size);
	bool SampleLayerTiles(ELayerGroup Group, const ivec2 &TopLeftTile, const ivec2 &Size, SBrushLayer &OutLayer) const;
	struct SDrawSegment
	{
		vec2 m_Start;
		vec2 m_End;
		ColorRGBA m_Color;
		int m_ExpireTick = 0;
		int m_ClientId = -1;
	};

	struct SDrawText
	{
		vec2 m_Pos;
		std::string m_Text;
		std::string m_PlayerName;
		ColorRGBA m_Color;
		int m_ExpireTick = 0;
		int m_ClientId = -1;
	};

	void ClearBrush(SState &State);
	bool ApplyBrushAtCursor(SState &State);
	bool ApplyBrushTiledSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size);
	bool ApplyDestructiveAirSelection(SState &State, const ivec2 &TopLeftTile, const ivec2 &Size, ELayerGroup Layer);
	bool RotateBrush(SState &State, bool Clockwise);
	bool MirrorBrush(SState &State, bool Horizontal);
	void UpdateBrushPreview(SState &State);
	void RenderBrushOverlay(const SState &State) const;
	bool BrushLayerHasContent(const SBrushLayer &Layer) const;
	bool TileHasContent(ELayerGroup Group, const STileSample &Tile, const STeleSample &Tele) const;
	int LayerIndexForGroup(ELayerGroup Group) const;
	void BeginDrawStroke(SState &State, int Dummy);
	void EndDrawStroke(SState &State);
	void HandleDrawSampling(SState &State, int Dummy, bool ForceSample);
	void SubmitDrawSegment(const vec2 &Start, const vec2 &End, int Dummy);
	void AddDrawSegment(const vec2 &Start, const vec2 &End, const ColorRGBA &Color, int ClientId);
	void RenderDrawSegments();
	void PruneExpiredDrawSegments();
	void ClearDrawState(SState &State);
	void BeginDrawText(SState &State, const vec2 &Anchor);
	void CancelDrawText(SState &State);
	void SubmitDrawText(SState &State, int Dummy);
	void AddDrawText(const vec2 &Pos, const char *pPlayerName, const char *pText, const ColorRGBA &Color, int ClientId);
	void RenderDrawTexts();
	void PruneExpiredDrawTexts();
	void RenderDrawTextPreview(const SState &State) const;
	void RenderDiffOverlay(const SState &State) const;
	void EnsureDiffBaseline();
	void CaptureDiffBaseline();
	void InvalidateDiffBaseline();
	const char *LocalPlayerName(int Dummy) const;
	ColorRGBA DrawColorForDummy(int Dummy) const;

	std::vector<SDrawSegment> m_DrawSegments;
	std::vector<SDrawText> m_DrawTexts;

	struct SLayerDiffSnapshot
	{
		bool m_Valid = false;
		int m_Width = 0;
		int m_Height = 0;
		std::vector<STileSample> m_Tiles;
		std::vector<STeleSample> m_Tele;
		void Clear()
		{
			m_Valid = false;
			m_Width = 0;
			m_Height = 0;
			m_Tiles.clear();
			m_Tele.clear();
		}
	};
	std::array<SLayerDiffSnapshot, static_cast<int>(ELayerGroup::COUNT)> m_aDiffBaselines;
	bool m_DiffBaselineValid = false;
};

#endif
