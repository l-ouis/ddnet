/* (c) DDNet contributors. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_COMPONENTS_EDITORSPEC_H
#define GAME_CLIENT_COMPONENTS_EDITORSPEC_H

#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/input.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/mapitems.h>

#include <array>
#include <vector>

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
		vec2 m_CursorWorld = vec2(0.0f, 0.0f);
		ivec2 m_LastSentCursor = ivec2(0, 0);
		bool m_ToolPaletteActive = false;
		vec2 m_ToolPalettePos = vec2(0.0f, 0.0f);
		bool m_ToolPaletteDragging = false;
		vec2 m_ToolPaletteDragOffset = vec2(0.0f, 0.0f);
		int m_SelectedPrimaryTool = 0;
		bool m_Destructive = true;
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
		bool m_BrushPreviewValid = false;
		ivec2 m_BrushPreviewTile = ivec2(0, 0);
		bool m_BrushPainting = false;
		ivec2 m_LastBrushApplyTile = ivec2(-1, -1);
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
	void ClearBrush(SState &State);
	bool ApplyBrushAtCursor(SState &State);
	void UpdateBrushPreview(SState &State);
	void RenderBrushOverlay(const SState &State) const;
	bool BrushLayerHasContent(const SBrushLayer &Layer) const;
	int LayerIndexForGroup(ELayerGroup Group) const;
};

#endif
