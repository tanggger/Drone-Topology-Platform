# Wing-Net Omni

Wing-Net Omni is a backend-centered UAV swarm communication simulation and confrontation platform built on top of `ns-3.43`.

It is designed to support two major mission modes:

- **Cooperative mode**: our side knows the network structure and performs communication planning, recovery, and robustness evaluation.
- **Non-cooperative mode**: our side can only observe noisy communication traces from an adversarial network, infer its topology, identify key nodes, and evaluate strike effects.

This repository is not just a generic ns-3 workspace. The custom backend already implements:

- scene-aware communication simulation
- cooperative recovery and failover logic
- non-cooperative observation, topology inference, target recommendation, strike execution, and effect evaluation
- a Flask API for frontend integration

---

## Table of Contents

- [1. Project Scope](#1-project-scope)
- [2. Current Backend Status](#2-current-backend-status)
- [3. System Architecture](#3-system-architecture)
- [4. Core Backend Workflows](#4-core-backend-workflows)
- [5. Scene Modeling](#5-scene-modeling)
- [6. Cooperative Mode](#6-cooperative-mode)
- [7. Non-cooperative Mode](#7-non-cooperative-mode)
- [8. Algorithms Implemented](#8-algorithms-implemented)
- [9. API and Frontend Integration](#9-api-and-frontend-integration)
- [10. Outputs and Data Contracts](#10-outputs-and-data-contracts)
- [11. Repository Structure](#11-repository-structure)
- [12. Build and Run](#12-build-and-run)
- [13. Key Project Documents](#13-key-project-documents)

---

## 1. Project Scope

The backend supports three major capability lines.

### 1.1 Scene realism

The simulator supports four scene types:

- `urban`
- `forest`
- `lake`
- `open-field`

These scenes do not only change labels. They affect:

- propagation model behavior
- path loss and NLOS penalties
- connectivity range
- interference pressure
- observation difficulty
- recovery thresholds
- network-layer routing pressure

### 1.2 Cooperative communication

In cooperative mode, the system models a known friendly swarm network and supports:

- communication mode switching
- node failure and disturbance injection
- centralized, distributed, and hybrid recovery
- leader failover
- recovery action logging
- resilience metrics and timeline outputs

### 1.3 Non-cooperative inference and attack

In non-cooperative mode, the system supports the full chain:

1. observe noisy adversarial communication events
2. form observation windows
3. build edge evidence
4. infer adversarial topology
5. identify key nodes
6. recommend strike targets
7. execute `node_strike`
8. evaluate pre/post damage globally and locally

---

## 2. Current Backend Status

Relative to the execution plan in [`project_docs/TWO_WEEK_EXECUTION_PLAN.md`](project_docs/TWO_WEEK_EXECUTION_PLAN.md), the backend is effectively in the final integration phase.

### Completed

- unified scene environment layer
- cooperative mode closure
- non-cooperative observation/inference/attack/evaluation closure
- frontend-facing API aggregation
- structured JSON/CSV output contracts

### Mostly completed

- frontend-oriented data contracts
- project-level documentation
- final demo and visualization alignment

### Practical status

The backend is already suitable for:

- frontend integration
- scenario demonstrations
- algorithm comparison
- strike-effect validation
- resilience analysis

---

## 3. System Architecture

The custom backend is concentrated in `scratch/uav_ra/` and `api_server/`.

### 3.1 Main execution layers

#### `scratch/uav_ra/main.cc`

Responsibilities:

- parse runtime parameters
- choose operation mode
- wire together simulation stages
- start the main ns-3 execution flow

#### `scratch/uav_ra/simulation_setup.cc`

Responsibilities:

- construct scenario configuration
- initialize wireless models and protocol stack
- apply difficulty and realism parameters
- load map/geometry-dependent scene settings

#### `scratch/uav_ra/scenario_environment.cc`

Responsibilities:

- load and interpret map geometry
- manage trajectory and environment interaction
- apply scene overlays such as buildings, vegetation, and water effects

#### `scratch/uav_ra/topology_control.cc`

Responsibilities:

- topology update
- link estimation
- cooperative control
- recovery decision logic
- route/relay/control-pressure metrics

#### `scratch/uav_ra/traffic_metrics.cc`

Responsibilities:

- traffic generation and TDMA
- QoS metrics
- topology and transmission logging
- flow-level runtime statistics

#### `scratch/uav_ra/non_cooperative_inference.cc`

Responsibilities:

- observation window processing
- edge evidence fusion
- causal topology inference
- dynamic edge tracking
- false-link suppression

#### `scratch/uav_ra/non_cooperative_attack.cc`

Responsibilities:

- key-node ranking
- target recommendation
- observed-track to entity binding
- strike execution
- effect evaluation

#### `scratch/uav_ra/output_runtime.cc`

Responsibilities:

- initialize output files
- write structured JSON and CSV outputs
- aggregate runtime metrics for frontend consumption

#### `api_server/app.py`

Responsibilities:

- expose HTTP endpoints
- launch simulations
- aggregate outputs into frontend-friendly payloads
- provide manifests and structured result loading

### 3.2 Data flow

The backend follows this high-level flow:

1. frontend or CLI provides scenario and mode parameters
2. backend builds scene and radio environment
3. ns-3 runs mobility, communication, and logging
4. cooperative or non-cooperative specialized logic runs
5. structured outputs are written as JSON/CSV
6. API aggregates outputs into `/frontend` payloads

---

## 4. Core Backend Workflows

## 4.1 Cooperative workflow

The cooperative pipeline is:

1. initialize known friendly swarm topology
2. run communication planning under a selected communication mode
3. inject failures or disturbances
4. detect degradation
5. trigger recovery
6. execute recovery actions
7. evaluate recovery completion and stabilization
8. export timeline and metrics

Supported communication modes:

- `centralized`
- `distributed`
- `hybrid`

Supported failure classes:

- `node_failure`
- `environment_degradation`
- `external_interference`
- `link_degradation`

Core outputs:

- `cooperative_mode_summary.json`
- `cooperative_failure_timeline.json`
- `cooperative_recovery_timeline.json`
- `cooperative_metrics_timeseries.json`
- `cooperative_dashboard_snapshot.json`
- `cooperative_failure_events.csv`
- `cooperative_recovery_actions.csv`
- `cooperative_recovery_metrics.csv`
- `cooperative_decision_trace.csv`

## 4.2 Non-cooperative workflow

The non-cooperative pipeline is:

1. observe noisy adversarial communication traces
2. group traces into observation windows
3. construct edge evidence
4. infer the adversarial communication graph
5. rank key nodes
6. recommend strike targets
7. bind observed tracks to executable entities
8. execute strike
9. evaluate pre/post damage

Core outputs:

- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`
- `noncooperative_attack_recommendations.csv`
- `noncooperative_attack_plan.json`
- `noncooperative_attack_events.csv`
- `noncooperative_target_binding.csv`
- `noncooperative_attack_effect_metrics.csv`
- `noncooperative_pre_post_comparison.json`

---

## 5. Scene Modeling

Scene realism has already been extended beyond static presets.

### 5.1 Urban

Implemented effects:

- building-aware propagation
- altitude-aware communication penalty/gain
- street canyon factor
- geometry-backed building statistics

Typical outputs include:

- `avgBuildingHeightM`
- `avgStreetWidthM`
- `urbanAltitudePenaltyDbLow`
- `urbanAltitudeGainDbHigh`
- `urbanStreetCanyonFactor`

### 5.2 Forest

Implemented effects:

- vegetation attenuation overlays
- stronger observation difficulty
- frequency/bandwidth/polarization-aware vegetation loss

Typical outputs include:

- `vegetationLossDbPerM`
- `carrierFrequencyGHz`
- `channelBandwidthMHz`
- `polarizationMode`

### 5.3 Lake

Implemented effects:

- `TwoRayGround`-based open-water propagation
- water-surface volatility
- deep fade probability
- reflection delay jitter

Typical outputs include:

- `lakeVolatilityJitterDb`
- `lakeDeepFadeProbability`
- `lakeDeepFadeMaxDb`
- `lakeReflectionDelayJitterMs`

### 5.4 Open-field

Implemented effects:

- baseline low-obstruction communication environment
- lower geometry complexity
- comparison-friendly baseline topology behavior

### 5.5 Network-layer realism

Scene impact is not only applied at the PHY layer. The backend also tracks:

- route changes
- relay switching
- control deadline misses
- route pressure

These metrics help expose whether a scene increases re-routing and coordination pressure.

Related realism summary:

- [`project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md`](project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md)

---

## 6. Cooperative Mode

The cooperative backend is structured around recovery, resilience, and explainability.

### 6.1 Recovery logic

The system supports:

- global recovery
- local recovery
- leader failover
- frozen failure-neighborhood evaluation
- network-level recovery pressure metrics

### 6.2 Recovery evaluation

Recovery completion is not decided by a single metric. It combines:

- global connectivity
- local or failure-neighborhood QoS
- recovery/stabilization timing

### 6.3 What the frontend can show

The frontend can directly render:

- current mode summary
- leader/backup leader
- failure timeline
- recovery actions timeline
- connectivity/PDR/throughput/delay curves
- failure-neighborhood metrics
- failure-target metrics
- route and relay pressure metrics

Detailed cooperative closure contract:

- [`project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md`](project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md)

---

## 7. Non-cooperative Mode

The non-cooperative backend now consists of two layers:

### 7.1 Observation and inference layer

This layer provides:

- observation events
- observation windows
- edge evidence
- inferred edges
- inferred graph nodes
- key node candidates

### 7.2 Attack closure layer

This layer provides:

- recommendation records
- strike plan
- binding records
- strike events
- global effect metrics
- target-neighborhood effect metrics
- pre/post comparison summary

The strike closure is already usable for:

- recommended target evaluation
- manual target execution
- scenario comparison
- damage-effect analysis

Related closure document:

- [`project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md`](project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)

---

## 8. Algorithms Implemented

This section focuses on the backend algorithms that matter for understanding the system logic.

## 8.1 Cooperative algorithms

The cooperative side uses a rule-driven cross-layer recovery controller rather than a single monolithic optimizer.

Implemented logic includes:

- mode-specific control branching
- failover handling
- frozen local recovery scope
- route/relay pressure tracking
- recovery completion and stabilization evaluation

This is closer to an explainable engineering control baseline than to a learned policy.

## 8.2 Non-cooperative inference algorithms

The non-cooperative side already includes two rounds of algorithm enhancement.

### Baseline layer

- observation window aggregation
- edge evidence fusion
- weighted structural node scoring

### First enhancement layer

- causal edge scores
- temporal continuity priors
- two-stage false-link suppression
- multi-feature key-node fusion
- local bridge and post-removal damage scoring

### Second enhancement layer

- directed causal edge inference
- conditional false-link suppression
- dynamic edge-state tracking
- stronger local subgraph key-node features

Implemented score families include:

- `laggedPredictiveScoreForward`
- `laggedPredictiveScoreBackward`
- `directedResponseScoreForward`
- `directedResponseScoreBackward`
- `excitationScoreForward`
- `excitationScoreBackward`
- `directionalityScore`
- `dominantDirection`
- `posteriorEdgeProbability`
- `edgeDynamicState`
- `stabilityAge`
- `weakeningAge`
- `directionalInfluenceScore`
- `twoHopReachabilityScore`
- `interClusterBridgeScore`
- `localCutRiskScore`
- `neighborRedundancyPenalty`

Current recommendation method version:

- `directed_dynamic_graph_bridge_fusion_v4`

Algorithm design notes:

- [`project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md`](project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md)

## 8.3 Recommendation effectiveness evaluation

The repository also contains a script to evaluate whether the current recommendation algorithm is actually useful:

- recommended target
- structure-only baseline
- random baseline
- oracle best candidate

This is used to judge whether algorithm strengthening is still needed.

Related script:

- `tools/run_recommendation_effectiveness_evaluation.py`

---

## 9. API and Frontend Integration

The backend already provides a frontend-oriented API layer.

### 9.1 Main endpoints

- `GET /api/health`
- `POST /api/simulate`
- `GET /api/results/<task_id>/frontend`
- `GET /api/results/<task_id>/manifest`
- `GET /api/results/<task_id>`
- `GET /api/maps`
- `GET /api/map_data/<map_name>`
- `POST /api/upload_osm`

### 9.2 Frontend result payload

The main result payload is:

- `meta`
- `shared`
- `cooperative`
- `non_cooperative`
- `manifest`

### 9.3 Frontend documentation

Frontend-oriented project docs are under:

- [`project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md`](project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md)
- [`project_docs/frontend/FRONTEND_FIELD_REFERENCE.md`](project_docs/frontend/FRONTEND_FIELD_REFERENCE.md)
- [`project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md`](project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md)

---

## 10. Outputs and Data Contracts

The backend produces both raw and frontend-oriented outputs.

### 10.1 Shared outputs

- `rtk-node-positions.csv`
- `rtk-node-transmissions.csv`
- `rtk-topology-changes.txt`
- `resource_allocation.csv`
- `resource_allocation_detailed.csv`
- `qos_performance.csv`
- `topology_evolution.csv`
- `topology_detailed.csv`
- `rtk-flow-stats.csv`
- `environment_summary.json`

### 10.2 Cooperative outputs

- `cooperative_mode_summary.json`
- `cooperative_failure_timeline.json`
- `cooperative_recovery_timeline.json`
- `cooperative_metrics_timeseries.json`
- `cooperative_dashboard_snapshot.json`
- `cooperative_failure_events.csv`
- `cooperative_recovery_actions.csv`
- `cooperative_recovery_metrics.csv`
- `cooperative_decision_trace.csv`

### 10.3 Non-cooperative outputs

- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`
- `noncooperative_attack_recommendations.csv`
- `noncooperative_attack_plan.json`
- `noncooperative_attack_events.csv`
- `noncooperative_target_binding.csv`
- `noncooperative_attack_effect_metrics.csv`
- `noncooperative_pre_post_comparison.json`

---

## 11. Repository Structure

The directories that matter most for this project are:

```text
api_server/                 Flask API for frontend integration
data_map/                   scene maps and geometry inputs
data_rtk/                   trajectory inputs
project_docs/               design, closure, algorithm, and frontend docs
scratch/uav_ra/             custom ns-3 backend implementation
tools/                      validation and evaluation scripts
rtk/                        trajectory generation and preprocessing
visualization/              plotting and replay helpers
```

The upstream ns-3 source tree under `src/`, `examples/`, `utils/`, and related directories remains intact.

---

## 12. Build and Run

## 12.1 Build

```bash
./ns3 build uav_resource_allocation
```

## 12.2 Start backend API

```bash
python3 api_server/app.py
```

Default API address:

```text
http://0.0.0.0:5000
```

## 12.3 Run validation

Full feature validation:

```bash
python3 tools/run_current_feature_validation.py
```

Recommendation effectiveness evaluation:

```bash
python3 tools/run_recommendation_effectiveness_evaluation.py
```

---

## 13. Key Project Documents

Execution and closure documents:

- [`project_docs/TWO_WEEK_EXECUTION_PLAN.md`](project_docs/TWO_WEEK_EXECUTION_PLAN.md)
- [`project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md`](project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md)
- [`project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md`](project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)
- [`project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md`](project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md)
- [`project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md`](project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md)

Frontend integration docs:

- [`project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md`](project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md)
- [`project_docs/frontend/FRONTEND_FIELD_REFERENCE.md`](project_docs/frontend/FRONTEND_FIELD_REFERENCE.md)
- [`project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md`](project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md)

---

## Final Note

This README is meant to help frontend engineers and project collaborators understand the overall backend logic quickly.

If you need:

- exact API request and response shapes, read the frontend API guide
- field-by-field semantics, read the frontend field reference
- page/component integration order, read the frontend handoff checklist

The backend is already in a state where frontend integration can proceed directly.
