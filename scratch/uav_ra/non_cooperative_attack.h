#ifndef UAV_RA_NON_COOPERATIVE_ATTACK_H
#define UAV_RA_NON_COOPERATIVE_ATTACK_H

#include "context.h"

void InitializeNonCooperativeAttackState();
void UpdateNonCooperativeAttackRecommendations();
bool BuildCurrentNonCooperativeAttackPlan(NonCooperativeAttackPlan& plan);
bool TryBindObservedTargetForStrike(uint32_t observedNodeId,
                                    NonCooperativeTargetBindingResult& bindingResult);
void MonitorNonCooperativeAttackExecution();
void MonitorNonCooperativeAttackEffectMetrics();
bool IsNonCooperativeObservedTrackStruck(uint32_t observedNodeId);
bool IsNonCooperativeTargetObjectStruck(uint32_t targetObjectKey);
bool IsNonCooperativeEntityNodeStruck(uint32_t interferenceNodeId);

#endif
