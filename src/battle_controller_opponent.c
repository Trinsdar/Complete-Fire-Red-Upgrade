#include "defines.h"
#include "defines_battle.h"
#include "../include/event_data.h"
#include "../include/random.h"

#include "../include/new/ai_util.h"
#include "../include/new/ai_master.h"
#include "../include/new/battle_controller_opponent.h"
#include "../include/new/battle_util.h"
#include "../include/new/frontier.h"
#include "../include/new/mega.h"
#include "../include/new/move_menu.h"
#include "../include/new/switching.h"
/*
battle_controller_opponent.c
	handles the functions responsible for the user moving between battle menus, choosing moves, etc.
*/

//TODO: Update Acupressure Targeting for AI

//This file's functions:
static void TryRechoosePartnerMove(u16 chosenMove);
static u8 LoadCorrectTrainerPicId(void);

void OpponentHandleChooseMove(void)
{
	u8 chosenMoveId;
	struct ChooseMoveStruct* moveInfo = (struct ChooseMoveStruct*)(&gBattleBufferA[gActiveBattler][4]);

	#ifdef VAR_GAME_DIFFICULTY
	u8 difficulty = VarGet(VAR_GAME_DIFFICULTY);
	#endif

	if ((gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_OAK_TUTORIAL | BATTLE_TYPE_SAFARI | BATTLE_TYPE_ROAMER))
	#ifdef FLAG_SMART_WILD
	||   FlagGet(FLAG_SMART_WILD)
	#endif
	#ifdef VAR_GAME_DIFFICULTY //Wild Pokemon are smart in expert mode
	||   difficulty == OPTIONS_EXPERT_DIFFICULTY
	#endif
	||	 WildMonIsSmart(gActiveBattler)
	||  (IsRaidBattle() && gRaidBattleStars >= 6))
	{
		if (RAID_BATTLE_END)
			goto CHOOSE_DUMB_MOVE;
	
		BattleAI_SetupAIData(0xF);
		chosenMoveId = BattleAI_ChooseMoveOrAction();

		switch (chosenMoveId) {
			case AI_CHOICE_WATCH:
				EmitTwoReturnValues(1, ACTION_WATCHES_CAREFULLY, 0);
				break;

			case AI_CHOICE_FLEE:
				EmitTwoReturnValues(1, ACTION_RUN, 0);
				break;

			case 6:
				EmitTwoReturnValues(1, 15, gBankTarget);
				break;

			default: ;
				u16 chosenMove = moveInfo->moves[chosenMoveId];

				if (gBattleMoves[chosenMove].target & MOVE_TARGET_USER)
				{
					gBankTarget = gActiveBattler;
				}
				else if (gBattleMoves[chosenMove].target & MOVE_TARGET_USER_OR_PARTNER)
				{
					if (SIDE(gBankTarget) != SIDE(gActiveBattler))
						gBankTarget = gActiveBattler;
				}
				else if (gBattleMoves[chosenMove].target & MOVE_TARGET_BOTH)
				{
					if (SIDE(gActiveBattler) == B_SIDE_PLAYER)
					{
						gBankTarget = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
						if (gAbsentBattlerFlags & gBitTable[gBankTarget])
							gBankTarget = GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT);
					}
					else
					{
						gBankTarget = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
						if (gAbsentBattlerFlags & gBitTable[gBankTarget])
							gBankTarget = GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT);
					}
				}

				if (moveInfo->possibleZMoves[chosenMoveId])
				{
					if (ShouldAIUseZMove(gActiveBattler, gBankTarget, moveInfo->moves[chosenMoveId]))
						gNewBS->ZMoveData->toBeUsed[gActiveBattler] = TRUE;
				}
				else if (ShouldAIDynamax(gActiveBattler, gBankTarget, chosenMove))
					gNewBS->dynamaxData.toBeUsed[gActiveBattler] = TRUE;
				else if (!ShouldAIDelayMegaEvolution(gActiveBattler, gBankTarget, chosenMove))
				{
					if (moveInfo->canMegaEvolve && moveInfo->megaVariance != MEGA_VARIANT_ULTRA_BURST)
						gNewBS->MegaData->chosen[gActiveBattler] = TRUE;
					else if (moveInfo->canMegaEvolve && moveInfo->megaVariance == MEGA_VARIANT_ULTRA_BURST)
						gNewBS->UltraData->chosen[gActiveBattler] = TRUE;
				}
				

				//This is handled again later, but it's only here to help with the case of choosing Helping Hand when the partner is switching out.
				gBattleStruct->chosenMovePositions[gActiveBattler] = chosenMoveId;
				gBattleStruct->moveTarget[gActiveBattler] = gBankTarget;
				gChosenMovesByBanks[gActiveBattler] = chosenMove;
				TryRemoveDoublesKillingScore(gActiveBattler, gBankTarget, chosenMove);

				EmitMoveChosen(1, chosenMoveId, gBankTarget, gNewBS->MegaData->chosen[gActiveBattler], gNewBS->UltraData->chosen[gActiveBattler], gNewBS->ZMoveData->toBeUsed[gActiveBattler], gNewBS->dynamaxData.toBeUsed[gActiveBattler]);
				TryRechoosePartnerMove(moveInfo->moves[chosenMoveId]);
				break;
		}

		OpponentBufferExecCompleted();
	}
	else
	{
		CHOOSE_DUMB_MOVE: ;
		u16 move;
		do
		{
			chosenMoveId = Random() & 3;
			move = moveInfo->moves[chosenMoveId];
		} while (move == MOVE_NONE);

		if (gBattleMoves[move].target & (MOVE_TARGET_USER_OR_PARTNER | MOVE_TARGET_USER))
			EmitMoveChosen(1, chosenMoveId, gActiveBattler, 0, 0, 0, FALSE);
		else if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
			EmitMoveChosen(1, chosenMoveId, GetBattlerAtPosition(Random() & 2), 0, 0, 0, FALSE);
		else
			EmitMoveChosen(1, chosenMoveId, FOE(gActiveBattler), 0, 0, 0, FALSE);

		OpponentBufferExecCompleted();
	}
}

#define STATE_BEFORE_ACTION_CHOSEN 0
static void TryRechoosePartnerMove(u16 chosenMove)
{
	if (GetBattlerPosition(gActiveBattler) & BIT_FLANK) //Second to choose action on either side
	{
		switch (gChosenMovesByBanks[PARTNER(gActiveBattler)]) {
			case MOVE_HELPINGHAND:
				if (chosenMove == MOVE_NONE || SPLIT(chosenMove) == SPLIT_STATUS)
				{
					struct ChooseMoveStruct moveInfo;
					gChosenMovesByBanks[gActiveBattler] = chosenMove;

					u8 backup = gActiveBattler;
					gActiveBattler = PARTNER(gActiveBattler);
					EmitChooseMove(0, (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) != 0, FALSE, &moveInfo); //Rechoose partner move
					MarkBufferBankForExecution(gActiveBattler);
					gActiveBattler = backup;
				}
				break;
		}
	}
}

void OpponentHandleDrawTrainerPic(void)
{
	u32 trainerPicId = LoadCorrectTrainerPicId();
	s16 xPos;

	if (gBattleTypeFlags & (BATTLE_TYPE_MULTI | BATTLE_TYPE_TWO_OPPONENTS))
	{
		if ((GetBattlerPosition(gActiveBattler) & BIT_FLANK) != 0) // second mon
			xPos = 152;
		else // first mon
			xPos = 200;
	}
	else
	{
		xPos = 176;
	}

	DecompressTrainerFrontPic(trainerPicId, gActiveBattler); //0x80346C4
	SetMultiuseSpriteTemplateToTrainerBack(trainerPicId, GetBattlerPosition(gActiveBattler)); //0x803F864
	gBattlerSpriteIds[gActiveBattler] = CreateSprite(&gMultiuseSpriteTemplate[0], //0x8006F8C
											   xPos,
											   (8 - gTrainerFrontPicCoords[trainerPicId].coords) * 4 + 40,
											   GetBattlerSpriteSubpriority(gActiveBattler)); //0x807685C

	gSprites[gBattlerSpriteIds[gActiveBattler]].pos2.x = -240;
	gSprites[gBattlerSpriteIds[gActiveBattler]].data[0] = 2;
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.paletteNum = IndexOfSpritePaletteTag(gTrainerFrontPicPaletteTable[trainerPicId].tag); //0x80089E8
	gSprites[gBattlerSpriteIds[gActiveBattler]].data[5] = gSprites[gBattlerSpriteIds[gActiveBattler]].oam.tileNum;
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.tileNum = GetSpriteTileStartByTag(gTrainerFrontPicTable[trainerPicId].tag); //0x8008804
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.affineParam = trainerPicId;
	gSprites[gBattlerSpriteIds[gActiveBattler]].callback = sub_8033EEC; //sub_805D7AC in Emerald

	gBattleBankFunc[gActiveBattler] = (u32) CompleteOnBattlerSpriteCallbackDummy; //0x8035AE8
}

void OpponentHandleTrainerSlide(void)
{

	u32 trainerPicId = LoadCorrectTrainerPicId();

	DecompressTrainerFrontPic(trainerPicId, gActiveBattler);
	SetMultiuseSpriteTemplateToTrainerBack(trainerPicId, GetBattlerPosition(gActiveBattler));
	gBattlerSpriteIds[gActiveBattler] = CreateSprite(&gMultiuseSpriteTemplate[0], 176, (8 - gTrainerFrontPicCoords[trainerPicId].coords) * 4 + 40, 0x1E);

	gSprites[gBattlerSpriteIds[gActiveBattler]].pos2.x = 96;
	gSprites[gBattlerSpriteIds[gActiveBattler]].pos1.x += 32;
	gSprites[gBattlerSpriteIds[gActiveBattler]].data[0] = -2;
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.paletteNum = IndexOfSpritePaletteTag(gTrainerFrontPicPaletteTable[trainerPicId].tag);
	gSprites[gBattlerSpriteIds[gActiveBattler]].data[5] = gSprites[gBattlerSpriteIds[gActiveBattler]].oam.tileNum;
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.tileNum = GetSpriteTileStartByTag(gTrainerFrontPicTable[trainerPicId].tag);
	gSprites[gBattlerSpriteIds[gActiveBattler]].oam.affineParam = trainerPicId;
	gSprites[gBattlerSpriteIds[gActiveBattler]].callback = sub_8033EEC;

	gBattleBankFunc[gActiveBattler] = (u32) CompleteOnBankSpriteCallbackDummy2;
}

extern u32 break_helper(u32 a);
extern u32 break_helper2(u32 a);

void OpponentHandleChoosePokemon(void)
{
	u8 chosenMonId;

	if (gBattleStruct->switchoutIndex[SIDE(gActiveBattler)] == PARTY_SIZE)
	{
		u8 battlerIn1, battlerIn2, firstId, lastId;
		pokemon_t* party = LoadPartyRange(gActiveBattler, &firstId, &lastId);

		if (gNewBS->ai.bestMonIdToSwitchInto[gActiveBattler][0] == PARTY_SIZE
		||  GetMonData(&party[gNewBS->ai.bestMonIdToSwitchInto[gActiveBattler][0]], MON_DATA_HP, NULL) == 0) //Best mon is dead
			CalcMostSuitableMonToSwitchInto();

		chosenMonId = GetMostSuitableMonToSwitchInto();

		if (chosenMonId == PARTY_SIZE)
		{
			if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
			{
				battlerIn1 = gActiveBattler;
				if (gAbsentBattlerFlags & gBitTable[PARTNER(gActiveBattler)])
					battlerIn2 = gActiveBattler;
				else
					battlerIn2 = PARTNER(battlerIn1);
			}
			else
			{
				battlerIn1 = gActiveBattler;
				battlerIn2 = gActiveBattler;
			}

			for (chosenMonId = firstId; chosenMonId < lastId; ++chosenMonId)
			{
				if (party[chosenMonId].species != SPECIES_NONE
				&& party[chosenMonId].hp != 0
				&& !GetMonData(&party[chosenMonId], MON_DATA_IS_EGG, 0)
				&& chosenMonId != gBattlerPartyIndexes[battlerIn1]
				&& chosenMonId != gBattlerPartyIndexes[battlerIn2])
					break;
			}
		}
	}
	else
	{
		chosenMonId = gBattleStruct->switchoutIndex[SIDE(gActiveBattler)];
		gBattleStruct->switchoutIndex[SIDE(gActiveBattler)] = PARTY_SIZE;
	}

	RemoveBestMonToSwitchInto(gActiveBattler);
	gBattleStruct->monToSwitchIntoId[gActiveBattler] = chosenMonId;
	EmitChosenMonReturnValue(1, chosenMonId, 0);
	OpponentBufferExecCompleted();
	TryRechoosePartnerMove(MOVE_NONE);
}

static u8 LoadCorrectTrainerPicId(void) {
	u8 trainerPicId;

	if (gTrainerBattleOpponent_A == 0x400) //Was Secret Base in Ruby
	{
		trainerPicId = GetSecretBaseTrainerPicIndex();
	}
  /*else if (gTrainerBattleOpponent_A == TRAINER_FRONTIER_BRAIN)
	{
		trainerPicId = GetFrontierBrainTrainerPicIndex();
	}*/
	else if (gBattleTypeFlags & BATTLE_TYPE_FRONTIER)
	{
		if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
		{
			if (gActiveBattler == 1)
				trainerPicId = GetFrontierTrainerFrontSpriteId(gTrainerBattleOpponent_A, 0);
			else
				trainerPicId = GetFrontierTrainerFrontSpriteId(VarGet(VAR_SECOND_OPPONENT), 1);
		}
		else
		{
			trainerPicId = GetFrontierTrainerFrontSpriteId(gTrainerBattleOpponent_A, 0);
		}
	}
	else if (gBattleTypeFlags & BATTLE_TYPE_TRAINER_TOWER)
	{
		trainerPicId = GetTrainerTowerTrainerPicIndex(); //0x815DA3C
	}
	else if (gBattleTypeFlags & BATTLE_TYPE_EREADER_TRAINER)
	{
		trainerPicId = GetEreaderTrainerFrontSpriteId(); //0x80E7420
	}
	else if (gBattleTypeFlags & BATTLE_TYPE_TWO_OPPONENTS)
	{
		if (gActiveBattler == 1)
			trainerPicId = gTrainers[gTrainerBattleOpponent_A].trainerPic;
		else
			trainerPicId = gTrainers[VarGet(VAR_SECOND_OPPONENT)].trainerPic;
	}
	else
	{
		trainerPicId = gTrainers[gTrainerBattleOpponent_A].trainerPic;
	}

	return trainerPicId;
}
