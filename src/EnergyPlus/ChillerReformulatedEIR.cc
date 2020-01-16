// EnergyPlus, Copyright (c) 1996-2020, The Board of Trustees of the University of Illinois,
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy), Oak Ridge
// National Laboratory, managed by UT-Battelle, Alliance for Sustainable Energy, LLC, and other
// contributors. All rights reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C++ Headers
#include <cassert>
#include <cmath>
#include <string>

// ObjexxFCL Headers
#include <ObjexxFCL/Fmath.hh>
#include <ObjexxFCL/gio.hh>

// EnergyPlus Headers
#include <EnergyPlus/BranchNodeConnections.hh>
#include <EnergyPlus/ChillerReformulatedEIR.hh>
#include <EnergyPlus/CurveManager.hh>
#include <EnergyPlus/DataBranchAirLoopPlant.hh>
#include <EnergyPlus/DataEnvironment.hh>
#include <EnergyPlus/DataHVACGlobals.hh>
#include <EnergyPlus/DataIPShortCuts.hh>
#include <EnergyPlus/DataLoopNode.hh>
#include <EnergyPlus/DataPlant.hh>
#include <EnergyPlus/DataSizing.hh>
#include <EnergyPlus/EMSManager.hh>
#include <EnergyPlus/FaultsManager.hh>
#include <EnergyPlus/FluidProperties.hh>
#include <EnergyPlus/General.hh>
#include <EnergyPlus/GlobalNames.hh>
#include <EnergyPlus/InputProcessing/InputProcessor.hh>
#include <EnergyPlus/NodeInputManager.hh>
#include <EnergyPlus/OutputProcessor.hh>
#include <EnergyPlus/OutputReportPredefined.hh>
#include <EnergyPlus/PlantUtilities.hh>
#include <EnergyPlus/Psychrometrics.hh>
#include <EnergyPlus/ReportSizingManager.hh>
#include <EnergyPlus/ScheduleManager.hh>
#include <EnergyPlus/StandardRatings.hh>
#include <EnergyPlus/UtilityRoutines.hh>

namespace EnergyPlus {

namespace ChillerReformulatedEIR {

    // The Electric EIR and Reformulated EIR chiller models are similar.
    // They only differ in the independent variable used to evaluate the performance curves.
    // Since the Reformulated EIR chiller uses outlet condenser water temperature as an
    // independent variable, iteration is required to converge on a solution.

    // MODULE INFORMATION:
    //       AUTHOR         Lixing Gu
    //       DATE WRITTEN   August 2006
    //       MODIFIED       na
    //       RE-ENGINEERED  na

    //       MODIFIED
    //			Aug.  2014, Rongpeng Zhang, added An additional part-load performance curve type

    // PURPOSE OF THIS MODULE:
    //  This module simulates the performance of the electric vapor compression
    //  chiller using a reformulated model based on the DOE-2 EIR chiller.

    // METHODOLOGY EMPLOYED:
    //  Once the PlantLoopManager determines that the Reformulated EIR chiller
    //  is available to meet a loop cooling demand, it calls SimReformulatedEIRChiller
    //  which in turn calls the reformulated EIR chiller model.
    //  The ReformulatedEIR chiller model is based on polynomial fits of chiller
    //  performance data.

    // REFERENCES:
    // 1. Hydeman, M., P. Sreedharan, N. Webb, and S. Blanc. 2002. "Development and Testing of a Reformulated
    //    Regression-Based Electric Chiller Model". ASHRAE Transactions, HI-02-18-2, Vol 108, Part 2, pp. 1118-1127.

    // Chiller type parameters
    int const AirCooled(1);   // Air-cooled condenser currently not allowed
    int const WaterCooled(2); // Only water-cooled condensers are currently allowed
    int const EvapCooled(3);  // Evap-cooled condenser currently not allowed
    
    // chiller flow modes
    int const FlowModeNotSet(200);
    int const ConstantFlow(201);
    int const NotModulated(202);
    int const LeavingSetPointModulated(203);

    // chiller part load curve types
    int const PLR_LeavingCondenserWaterTemperature(1); // Type 1_LeavingCondenserWaterTemperature
    int const PLR_Lift(2);                             // Type 2_Lift

    // MODULE VARIABLE DECLARATIONS:
    int NumElecReformEIRChillers(0); // Number of electric reformulated EIR chillers specified in input

    bool GetInputREIR(true); // When TRUE, calls subroutine to read input file

    // Object Data
    Array1D<ReformulatedEIRChillerSpecs> ElecReformEIRChiller; // dimension to number of machines

    void SimReformulatedEIRChiller(std::string const &EP_UNUSED(EIRChillerType), // Type of chiller !unused1208
                                   std::string const &EIRChillerName,            // User specified name of chiller
                                   int const EquipFlowCtrl,                      // Flow control mode for the equipment
                                   int &CompIndex,                               // Chiller number pointer
                                   int const LoopNum,                            // plant loop index pointer
                                   bool const RunFlag,                           // Simulate chiller when TRUE
                                   bool const FirstIteration,                    // Initialize variables when TRUE
                                   bool &InitLoopEquip,                          // If not zero, calculate the max load for operating conditions
                                   Real64 &MyLoad,                               // Loop demand component will meet [W]
                                   Real64 &MaxCap,                               // Maximum operating capacity of chiller [W]
                                   Real64 &MinCap,                               // Minimum operating capacity of chiller [W]
                                   Real64 &OptCap,                               // Optimal operating capacity of chiller [W]
                                   bool const GetSizingFactor,                   // TRUE when just the sizing factor is requested
                                   Real64 &SizingFactor,                         // sizing factor
                                   Real64 &TempCondInDesign,
                                   Real64 &TempEvapOutDesign)
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR         Lixing Gu
        //       DATE WRITTEN   July 2004
        //       MODIFIED       na
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        //  This is the reformulated EIR chiller model driver. It gets the input for the
        //  models, initializes simulation variables, calls the appropriate model and sets
        //  up reporting variables.

        int EIRChillNum;

        if (GetInputREIR) {
            GetElecReformEIRChillerInput();
            GetInputREIR = false;
        }

        // Find the correct Chiller
        if (CompIndex == 0) {
            EIRChillNum = UtilityRoutines::FindItemInList(EIRChillerName, ElecReformEIRChiller);
            if (EIRChillNum == 0) {
                ShowFatalError("SimReformulatedEIRChiller: Specified Chiller not one of Valid Reformulated EIR Electric Chillers=" + EIRChillerName);
            }
            CompIndex = EIRChillNum;
        } else {
            EIRChillNum = CompIndex;
            if (EIRChillNum > NumElecReformEIRChillers || EIRChillNum < 1) {
                ShowFatalError("SimReformulatedEIRChiller:  Invalid CompIndex passed=" + General::TrimSigDigits(EIRChillNum) +
                               ", Number of Units=" + General::TrimSigDigits(NumElecReformEIRChillers) + ", Entered Unit name=" + EIRChillerName);
            }
            if (EIRChillerName != ElecReformEIRChiller(EIRChillNum).Name) {
                ShowFatalError("SimReformulatedEIRChiller: Invalid CompIndex passed=" + General::TrimSigDigits(EIRChillNum) + ", Unit name=" + EIRChillerName +
                               ", stored Unit Name for that index=" + ElecReformEIRChiller(EIRChillNum).Name);
            }
        }

        if (InitLoopEquip) {
            TempEvapOutDesign = ElecReformEIRChiller(EIRChillNum).TempRefEvapOut;
            TempCondInDesign = ElecReformEIRChiller(EIRChillNum).TempRefCondIn;
            InitElecReformEIRChiller(EIRChillNum, RunFlag, MyLoad);

            if (LoopNum == ElecReformEIRChiller(EIRChillNum).CWLoopNum) {
                SizeElecReformEIRChiller(EIRChillNum);
                MinCap = ElecReformEIRChiller(EIRChillNum).RefCap * ElecReformEIRChiller(EIRChillNum).MinPartLoadRat;
                MaxCap = ElecReformEIRChiller(EIRChillNum).RefCap * ElecReformEIRChiller(EIRChillNum).MaxPartLoadRat;
                OptCap = ElecReformEIRChiller(EIRChillNum).RefCap * ElecReformEIRChiller(EIRChillNum).OptPartLoadRat;
            } else {
                MinCap = 0.0;
                MaxCap = 0.0;
                OptCap = 0.0;
            }
            if (GetSizingFactor) {
                SizingFactor = ElecReformEIRChiller(EIRChillNum).SizFac;
            }
            return;
        }

        if (LoopNum == ElecReformEIRChiller(EIRChillNum).CWLoopNum) {
            InitElecReformEIRChiller(EIRChillNum, RunFlag, MyLoad);
            ControlReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl);
            UpdateReformEIRChillerRecords(MyLoad, RunFlag, EIRChillNum);
        } else if (LoopNum == ElecReformEIRChiller(EIRChillNum).CDLoopNum) {
            int LoopSide = ElecReformEIRChiller(EIRChillNum).CDLoopSideNum;
            PlantUtilities::UpdateChillerComponentCondenserSide(LoopNum,
                                                LoopSide,
                                                DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                                ElecReformEIRChiller(EIRChillNum).CondInletNodeNum,
                                                ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum,
                                                ElecReformEIRChiller(EIRChillNum).QCondenser,
                                                ElecReformEIRChiller(EIRChillNum).CondInletTemp,
                                                ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                                                ElecReformEIRChiller(EIRChillNum).CondMassFlowRate,
                                                FirstIteration);
        } else if (LoopNum == ElecReformEIRChiller(EIRChillNum).HRLoopNum) {
            PlantUtilities::UpdateComponentHeatRecoverySide(ElecReformEIRChiller(EIRChillNum).HRLoopNum,
                                            ElecReformEIRChiller(EIRChillNum).HRLoopSideNum,
                                            DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                            ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum,
                                            ElecReformEIRChiller(EIRChillNum).HeatRecOutletNodeNum,
                                            ElecReformEIRChiller(EIRChillNum).QHeatRecovery,
                                            ElecReformEIRChiller(EIRChillNum).HeatRecInletTemp,
                                            ElecReformEIRChiller(EIRChillNum).HeatRecOutletTemp,
                                            ElecReformEIRChiller(EIRChillNum).HeatRecMassFlow,
                                            FirstIteration);
        }
    }

    void GetElecReformEIRChillerInput()
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          Lixing Gu, FSEC
        //       DATE WRITTEN:    July 2006

        //       MODIFIED
        //			Aug.  2014, Rongpeng Zhang, added an additional part-load performance curve type

        // PURPOSE OF THIS SUBROUTINE:
        //  This routine will get the input required by the Reformulated Electric EIR Chiller model

        static std::string const RoutineName("GetElecReformEIRChillerInput: "); // include trailing blank space

        bool ErrorsFound(false);   // True when input errors found

        DataIPShortCuts::cCurrentModuleObject = "Chiller:Electric:ReformulatedEIR";
        NumElecReformEIRChillers = inputProcessor->getNumObjectsFound(DataIPShortCuts::cCurrentModuleObject);

        if (NumElecReformEIRChillers <= 0) {
            ShowSevereError("No " + DataIPShortCuts::cCurrentModuleObject + " equipment specified in input file");
            ErrorsFound = true;
        }

        // ALLOCATE ARRAYS
        ElecReformEIRChiller.allocate(NumElecReformEIRChillers);

        // Load arrays with reformulated electric EIR chiller data
        for (int EIRChillerNum = 1; EIRChillerNum <= NumElecReformEIRChillers; ++EIRChillerNum) {
            int NumAlphas;                    // Number of elements in the alpha array
            int NumNums;                      // Number of elements in the numeric array
            int IOStat;                       // IO Status when calling get input subroutine
            inputProcessor->getObjectItem(DataIPShortCuts::cCurrentModuleObject,
                                          EIRChillerNum,
                                          DataIPShortCuts::cAlphaArgs,
                                          NumAlphas,
                                          DataIPShortCuts::rNumericArgs,
                                          NumNums,
                                          IOStat,
                                          DataIPShortCuts::lNumericFieldBlanks,
                                          DataIPShortCuts::lAlphaFieldBlanks,
                                          DataIPShortCuts::cAlphaFieldNames,
                                          DataIPShortCuts::cNumericFieldNames);
            UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject, ErrorsFound);

            // ErrorsFound will be set to True if problem was found, left untouched otherwise
            GlobalNames::VerifyUniqueChillerName(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), ErrorsFound, DataIPShortCuts::cCurrentModuleObject + " Name");

            ElecReformEIRChiller(EIRChillerNum).Name = DataIPShortCuts::cAlphaArgs(1);
            // Performance curves
            ElecReformEIRChiller(EIRChillerNum).ChillerCapFTIndex = CurveManager::GetCurveIndex(DataIPShortCuts::cAlphaArgs(2));
            ElecReformEIRChiller(EIRChillerNum).CAPFTName = DataIPShortCuts::cAlphaArgs(2);
            if (ElecReformEIRChiller(EIRChillerNum).ChillerCapFTIndex == 0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(2) + '=' + DataIPShortCuts::cAlphaArgs(2));
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).ChillerEIRFTIndex = CurveManager::GetCurveIndex(DataIPShortCuts::cAlphaArgs(3));
            ElecReformEIRChiller(EIRChillerNum).EIRFTName = DataIPShortCuts::cAlphaArgs(3);
            if (ElecReformEIRChiller(EIRChillerNum).ChillerEIRFTIndex == 0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(3) + '=' + DataIPShortCuts::cAlphaArgs(3));
                ErrorsFound = true;
            }

            // The default type of part-load curve is: LeavingCondenserWaterTemperature
            std::string PartLoadCurveType;    // Part load curve type
            if (DataIPShortCuts::lAlphaFieldBlanks(4)) {
                PartLoadCurveType = "LeavingCondenserWaterTemperature";
            } else {
                PartLoadCurveType = DataIPShortCuts::cAlphaArgs(4);
            }

            ElecReformEIRChiller(EIRChillerNum).EIRFPLRName = DataIPShortCuts::cAlphaArgs(5);
            ElecReformEIRChiller(EIRChillerNum).ChillerEIRFPLRIndex = CurveManager::GetCurveIndex(DataIPShortCuts::cAlphaArgs(5));
            if (ElecReformEIRChiller(EIRChillerNum).ChillerEIRFPLRIndex == 0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(5) + '=' + DataIPShortCuts::cAlphaArgs(5));
                ErrorsFound = true;
            }

            // Check the type of part-load curves implemented: 1_LeavingCondenserWaterTemperature, 2_Lift    zrp_Aug2014
            if (UtilityRoutines::SameString(PartLoadCurveType, "LeavingCondenserWaterTemperature") &&
                CurveManager::PerfCurve(ElecReformEIRChiller(EIRChillerNum).ChillerEIRFPLRIndex).NumDims == 2) {
                ElecReformEIRChiller(EIRChillerNum).PartLoadCurveType = PLR_LeavingCondenserWaterTemperature;
            } else if (UtilityRoutines::SameString(PartLoadCurveType, "Lift") &&
                       CurveManager::PerfCurve(ElecReformEIRChiller(EIRChillerNum).ChillerEIRFPLRIndex).NumDims == 3) {
                ElecReformEIRChiller(EIRChillerNum).PartLoadCurveType = PLR_Lift;
            } else {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(5) + '=' + DataIPShortCuts::cAlphaArgs(5) + " for " + DataIPShortCuts::cAlphaFieldNames(4) + '=' + DataIPShortCuts::cAlphaArgs(4));
                ErrorsFound = true;
            }

            // Chilled water inlet/outlet node names are necessary
            if (DataIPShortCuts::lAlphaFieldBlanks(6)) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cAlphaFieldNames(6) + " is blank.");
                ErrorsFound = true;
            }
            if (DataIPShortCuts::lAlphaFieldBlanks(7)) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cAlphaFieldNames(7) + " is blank.");
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).EvapInletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(6), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Inlet, 1, DataLoopNode::ObjectIsNotParent);
            ElecReformEIRChiller(EIRChillerNum).EvapOutletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(7), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Outlet, 1, DataLoopNode::ObjectIsNotParent);
            BranchNodeConnections::TestCompSet(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cAlphaArgs(6), DataIPShortCuts::cAlphaArgs(7), "Chilled Water Nodes");

            ElecReformEIRChiller(EIRChillerNum).CondenserType = WaterCooled;

            // Condenser inlet/outlet node names are necessary
            if (DataIPShortCuts::lAlphaFieldBlanks(8)) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cAlphaFieldNames(8) + " is blank.");
                ErrorsFound = true;
            }
            if (DataIPShortCuts::lAlphaFieldBlanks(9)) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cAlphaFieldNames(9) + " is blank.");
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).CondInletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(8), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Inlet, 2, DataLoopNode::ObjectIsNotParent);
            ElecReformEIRChiller(EIRChillerNum).CondOutletNodeNum = NodeInputManager::GetOnlySingleNode(
                DataIPShortCuts::cAlphaArgs(9), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Outlet, 2, DataLoopNode::ObjectIsNotParent);

            BranchNodeConnections::TestCompSet(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cAlphaArgs(8), DataIPShortCuts::cAlphaArgs(9), "Condenser Water Nodes");

            {
                auto const SELECT_CASE_var(DataIPShortCuts::cAlphaArgs(10));
                if (SELECT_CASE_var == "CONSTANTFLOW") {
                    ElecReformEIRChiller(EIRChillerNum).FlowMode = ConstantFlow;
                } else if (SELECT_CASE_var == "LEAVINGSETPOINTMODULATED") {
                    ElecReformEIRChiller(EIRChillerNum).FlowMode = LeavingSetPointModulated;
                } else if (SELECT_CASE_var == "NOTMODULATED") {
                    ElecReformEIRChiller(EIRChillerNum).FlowMode = NotModulated;
                } else {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\",");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(10) + '=' + DataIPShortCuts::cAlphaArgs(10));
                    ShowContinueError("Available choices are ConstantFlow, NotModulated, or LeavingSetpointModulated");
                    ShowContinueError("Flow mode NotModulated is assumed and the simulation continues.");
                    ElecReformEIRChiller(EIRChillerNum).FlowMode = NotModulated;
                }
            }

            //   Chiller rated performance data
            ElecReformEIRChiller(EIRChillerNum).RefCap = DataIPShortCuts::rNumericArgs(1);
            if (ElecReformEIRChiller(EIRChillerNum).RefCap == DataSizing::AutoSize) {
                ElecReformEIRChiller(EIRChillerNum).RefCapWasAutoSized = true;
            }
            if (DataIPShortCuts::rNumericArgs(1) == 0.0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cNumericFieldNames(1) + '=' + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(1), 2));
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).RefCOP = DataIPShortCuts::rNumericArgs(2);
            if (DataIPShortCuts::rNumericArgs(2) == 0.0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError("Invalid " + DataIPShortCuts::cNumericFieldNames(2) + '=' + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(2), 2));
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).TempRefEvapOut = DataIPShortCuts::rNumericArgs(3);
            ElecReformEIRChiller(EIRChillerNum).TempRefCondOut = DataIPShortCuts::rNumericArgs(4);
            if (ElecReformEIRChiller(EIRChillerNum).TempRefEvapOut >= ElecReformEIRChiller(EIRChillerNum).TempRefCondOut) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(3) + " [" + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(3), 2) + "] >= " + DataIPShortCuts::cNumericFieldNames(4) + " [" +
                                  General::RoundSigDigits(DataIPShortCuts::rNumericArgs(4), 2) + ']');
                ShowContinueError("Reference Leaving Chilled Water Temperature must be less than Reference Leaving Condenser Water Temperature ");
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).EvapVolFlowRate = DataIPShortCuts::rNumericArgs(5);
            if (ElecReformEIRChiller(EIRChillerNum).EvapVolFlowRate == DataSizing::AutoSize) {
                ElecReformEIRChiller(EIRChillerNum).EvapVolFlowRateWasAutoSized = true;
            }
            ElecReformEIRChiller(EIRChillerNum).CondVolFlowRate = DataIPShortCuts::rNumericArgs(6);
            if (ElecReformEIRChiller(EIRChillerNum).CondVolFlowRate == DataSizing::AutoSize) {
                ElecReformEIRChiller(EIRChillerNum).CondVolFlowRateWasAutoSized = true;
            }
            ElecReformEIRChiller(EIRChillerNum).MinPartLoadRat = DataIPShortCuts::rNumericArgs(7);
            ElecReformEIRChiller(EIRChillerNum).MaxPartLoadRat = DataIPShortCuts::rNumericArgs(8);
            ElecReformEIRChiller(EIRChillerNum).OptPartLoadRat = DataIPShortCuts::rNumericArgs(9);
            ElecReformEIRChiller(EIRChillerNum).MinUnloadRat = DataIPShortCuts::rNumericArgs(10);
            ElecReformEIRChiller(EIRChillerNum).SizFac = DataIPShortCuts::rNumericArgs(14);
            if (ElecReformEIRChiller(EIRChillerNum).SizFac <= 0.0) ElecReformEIRChiller(EIRChillerNum).SizFac = 1.0;

            if (ElecReformEIRChiller(EIRChillerNum).MinPartLoadRat > ElecReformEIRChiller(EIRChillerNum).MaxPartLoadRat) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(7) + " [" + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(7), 3) + "] > " + DataIPShortCuts::cNumericFieldNames(8) + " [" +
                                  General::RoundSigDigits(DataIPShortCuts::rNumericArgs(8), 3) + ']');
                ShowContinueError("Minimum part load ratio must be less than or equal to the maximum part load ratio ");
                ErrorsFound = true;
            }

            if (ElecReformEIRChiller(EIRChillerNum).MinUnloadRat < ElecReformEIRChiller(EIRChillerNum).MinPartLoadRat ||
                ElecReformEIRChiller(EIRChillerNum).MinUnloadRat > ElecReformEIRChiller(EIRChillerNum).MaxPartLoadRat) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(10) + " = " + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(10), 3));
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(10) + " must be greater than or equal to the " + DataIPShortCuts::cNumericFieldNames(7));
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(10) + " must be less than or equal to the " + DataIPShortCuts::cNumericFieldNames(8));
                ErrorsFound = true;
            }

            if (ElecReformEIRChiller(EIRChillerNum).OptPartLoadRat < ElecReformEIRChiller(EIRChillerNum).MinPartLoadRat ||
                ElecReformEIRChiller(EIRChillerNum).OptPartLoadRat > ElecReformEIRChiller(EIRChillerNum).MaxPartLoadRat) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(9) + " = " + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(9), 3));
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(9) + " must be greater than or equal to the " + DataIPShortCuts::cNumericFieldNames(7));
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(9) + " must be less than or equal to the " + DataIPShortCuts::cNumericFieldNames(8));
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).CompPowerToCondenserFrac = DataIPShortCuts::rNumericArgs(11);

            if (ElecReformEIRChiller(EIRChillerNum).CompPowerToCondenserFrac < 0.0 ||
                ElecReformEIRChiller(EIRChillerNum).CompPowerToCondenserFrac > 1.0) {
                ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(11) + " = " + General::RoundSigDigits(DataIPShortCuts::rNumericArgs(11), 3));
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(11) + " must be greater than or equal to zero");
                ShowContinueError(DataIPShortCuts::cNumericFieldNames(11) + " must be less than or equal to one");
                ErrorsFound = true;
            }

            ElecReformEIRChiller(EIRChillerNum).TempLowLimitEvapOut = DataIPShortCuts::rNumericArgs(12);

            // These are the optional heat recovery inputs
            ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate = DataIPShortCuts::rNumericArgs(13);
            if (ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate == DataSizing::AutoSize) {
                ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRateWasAutoSized = true;
            }
            if ((ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate > 0.0) ||
                (ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate == DataSizing::AutoSize)) {
                ElecReformEIRChiller(EIRChillerNum).HeatRecActive = true;
                ElecReformEIRChiller(EIRChillerNum).HeatRecInletNodeNum = NodeInputManager::GetOnlySingleNode(
                    DataIPShortCuts::cAlphaArgs(11), ErrorsFound, DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Inlet, 3, DataLoopNode::ObjectIsNotParent);
                if (ElecReformEIRChiller(EIRChillerNum).HeatRecInletNodeNum == 0) {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(11) + '=' + DataIPShortCuts::cAlphaArgs(11));
                    ErrorsFound = true;
                }
                ElecReformEIRChiller(EIRChillerNum).HeatRecOutletNodeNum = NodeInputManager::GetOnlySingleNode(DataIPShortCuts::cAlphaArgs(12),
                                                                                             ErrorsFound,
                                                                                             DataIPShortCuts::cCurrentModuleObject,
                                                                                             DataIPShortCuts::cAlphaArgs(1),
                                                                                             DataLoopNode::NodeType_Water,
                                                                                             DataLoopNode::NodeConnectionType_Outlet,
                                                                                             3,
                                                                                             DataLoopNode::ObjectIsNotParent);
                if (ElecReformEIRChiller(EIRChillerNum).HeatRecOutletNodeNum == 0) {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                    ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(12) + '=' + DataIPShortCuts::cAlphaArgs(12));
                    ErrorsFound = true;
                }
                if (ElecReformEIRChiller(EIRChillerNum).CondenserType != WaterCooled) {
                    ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                    ShowContinueError("Heat Recovery requires a Water Cooled Condenser.");
                    ErrorsFound = true;
                }

                BranchNodeConnections::TestCompSet(DataIPShortCuts::cCurrentModuleObject, DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cAlphaArgs(11), DataIPShortCuts::cAlphaArgs(12), "Heat Recovery Nodes");

                if (ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate > 0.0) {
                    PlantUtilities::RegisterPlantCompDesignFlow(ElecReformEIRChiller(EIRChillerNum).HeatRecInletNodeNum,
                                                ElecReformEIRChiller(EIRChillerNum).DesignHeatRecVolFlowRate);
                }
                if (NumNums > 14) {
                    if (!DataIPShortCuts::lNumericFieldBlanks(15)) {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecCapacityFraction = DataIPShortCuts::rNumericArgs(15);
                    } else {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecCapacityFraction = 1.0;
                    }
                } else {
                    ElecReformEIRChiller(EIRChillerNum).HeatRecCapacityFraction = 1.0;
                }

                if (NumAlphas > 12) {
                    if (!DataIPShortCuts::lAlphaFieldBlanks(13)) {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecInletLimitSchedNum = ScheduleManager::GetScheduleIndex(DataIPShortCuts::cAlphaArgs(13));
                        if (ElecReformEIRChiller(EIRChillerNum).HeatRecInletLimitSchedNum == 0) {
                            ShowSevereError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                            ShowContinueError("Invalid " + DataIPShortCuts::cAlphaFieldNames(13) + '=' + DataIPShortCuts::cAlphaArgs(13));
                            ErrorsFound = true;
                        }
                    } else {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecInletLimitSchedNum = 0;
                    }
                } else {
                    ElecReformEIRChiller(EIRChillerNum).HeatRecInletLimitSchedNum = 0;
                }

                if (NumAlphas > 13) {
                    if (!DataIPShortCuts::lAlphaFieldBlanks(14)) {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecSetPointNodeNum = NodeInputManager::GetOnlySingleNode(DataIPShortCuts::cAlphaArgs(14),
                                                                                                       ErrorsFound,
                                                                                                       DataIPShortCuts::cCurrentModuleObject,
                                                                                                       DataIPShortCuts::cAlphaArgs(1),
                                                                                                       DataLoopNode::NodeType_Water,
                                                                                                       DataLoopNode::NodeConnectionType_Sensor,
                                                                                                       1,
                                                                                                       DataLoopNode::ObjectIsNotParent);
                    } else {
                        ElecReformEIRChiller(EIRChillerNum).HeatRecSetPointNodeNum = 0;
                    }
                } else {
                    ElecReformEIRChiller(EIRChillerNum).HeatRecSetPointNodeNum = 0;
                }

            } else {
                ElecReformEIRChiller(EIRChillerNum).HeatRecActive = false;
                ElecReformEIRChiller(EIRChillerNum).DesignHeatRecMassFlowRate = 0.0;
                ElecReformEIRChiller(EIRChillerNum).HeatRecInletNodeNum = 0;
                ElecReformEIRChiller(EIRChillerNum).HeatRecOutletNodeNum = 0;
                if ((!DataIPShortCuts::lAlphaFieldBlanks(11)) || (!DataIPShortCuts::lAlphaFieldBlanks(12))) {
                    ShowWarningError(RoutineName + DataIPShortCuts::cCurrentModuleObject + "=\"" + DataIPShortCuts::cAlphaArgs(1) + "\"");
                    ShowWarningError("Since Reference Heat Reclaim Volume Flow Rate = 0.0, heat recovery is inactive.");
                    ShowContinueError("However, node names were specified for heat recovery inlet or outlet nodes.");
                }
            }

            if (NumAlphas > 14) {
                ElecReformEIRChiller(EIRChillerNum).EndUseSubcategory = DataIPShortCuts::cAlphaArgs(15);
            } else {
                ElecReformEIRChiller(EIRChillerNum).EndUseSubcategory = "General";
            }
        }

        if (ErrorsFound) {
            ShowFatalError("Errors found in processing input for " + DataIPShortCuts::cCurrentModuleObject);
        }

        for (int EIRChillerNum = 1; EIRChillerNum <= NumElecReformEIRChillers; ++EIRChillerNum) {
            SetupOutputVariable("Chiller Part Load Ratio",
                                OutputProcessor::Unit::None,
                                ElecReformEIRChiller(EIRChillerNum).ChillerPartLoadRatio,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Cycling Ratio",
                                OutputProcessor::Unit::None,
                                ElecReformEIRChiller(EIRChillerNum).ChillerCyclingRatio,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Electric Power",
                                OutputProcessor::Unit::W,
                                ElecReformEIRChiller(EIRChillerNum).Power,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Electric Energy",
                                OutputProcessor::Unit::J,
                                ElecReformEIRChiller(EIRChillerNum).Energy,
                                "System",
                                "Sum",
                                ElecReformEIRChiller(EIRChillerNum).Name,
                                _,
                                "ELECTRICITY",
                                "Cooling",
                                ElecReformEIRChiller(EIRChillerNum).EndUseSubcategory,
                                "Plant");

            SetupOutputVariable("Chiller Evaporator Cooling Rate",
                                OutputProcessor::Unit::W,
                                ElecReformEIRChiller(EIRChillerNum).QEvaporator,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Evaporator Cooling Energy",
                                OutputProcessor::Unit::J,
                                ElecReformEIRChiller(EIRChillerNum).EvapEnergy,
                                "System",
                                "Sum",
                                ElecReformEIRChiller(EIRChillerNum).Name,
                                _,
                                "ENERGYTRANSFER",
                                "CHILLERS",
                                _,
                                "Plant");
            SetupOutputVariable("Chiller False Load Heat Transfer Rate",
                                OutputProcessor::Unit::W,
                                ElecReformEIRChiller(EIRChillerNum).ChillerFalseLoadRate,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller False Load Heat Transfer Energy",
                                OutputProcessor::Unit::J,
                                ElecReformEIRChiller(EIRChillerNum).ChillerFalseLoad,
                                "System",
                                "Sum",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Evaporator Inlet Temperature",
                                OutputProcessor::Unit::C,
                                ElecReformEIRChiller(EIRChillerNum).EvapInletTemp,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Evaporator Outlet Temperature",
                                OutputProcessor::Unit::C,
                                ElecReformEIRChiller(EIRChillerNum).EvapOutletTemp,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Evaporator Mass Flow Rate",
                                OutputProcessor::Unit::kg_s,
                                ElecReformEIRChiller(EIRChillerNum).EvapMassFlowRate,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);

            SetupOutputVariable("Chiller Condenser Heat Transfer Rate",
                                OutputProcessor::Unit::W,
                                ElecReformEIRChiller(EIRChillerNum).QCondenser,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Condenser Heat Transfer Energy",
                                OutputProcessor::Unit::J,
                                ElecReformEIRChiller(EIRChillerNum).CondEnergy,
                                "System",
                                "Sum",
                                ElecReformEIRChiller(EIRChillerNum).Name,
                                _,
                                "ENERGYTRANSFER",
                                "HEATREJECTION",
                                _,
                                "Plant");
            SetupOutputVariable("Chiller COP",
                                OutputProcessor::Unit::W_W,
                                ElecReformEIRChiller(EIRChillerNum).ActualCOP,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);

            SetupOutputVariable("Chiller Capacity Temperature Modifier Multiplier",
                                OutputProcessor::Unit::None,
                                ElecReformEIRChiller(EIRChillerNum).ChillerCapFT,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller EIR Temperature Modifier Multiplier",
                                OutputProcessor::Unit::None,
                                ElecReformEIRChiller(EIRChillerNum).ChillerEIRFT,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller EIR Part Load Modifier Multiplier",
                                OutputProcessor::Unit::None,
                                ElecReformEIRChiller(EIRChillerNum).ChillerEIRFPLR,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);

            SetupOutputVariable("Chiller Condenser Inlet Temperature",
                                OutputProcessor::Unit::C,
                                ElecReformEIRChiller(EIRChillerNum).CondInletTemp,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Condenser Outlet Temperature",
                                OutputProcessor::Unit::C,
                                ElecReformEIRChiller(EIRChillerNum).CondOutletTemp,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);
            SetupOutputVariable("Chiller Condenser Mass Flow Rate",
                                OutputProcessor::Unit::kg_s,
                                ElecReformEIRChiller(EIRChillerNum).CondMassFlowRate,
                                "System",
                                "Average",
                                ElecReformEIRChiller(EIRChillerNum).Name);

            // If heat recovery is active then setup report variables
            if (ElecReformEIRChiller(EIRChillerNum).HeatRecActive) {
                SetupOutputVariable("Chiller Total Recovered Heat Rate",
                                    OutputProcessor::Unit::W,
                                    ElecReformEIRChiller(EIRChillerNum).QHeatRecovery,
                                    "System",
                                    "Average",
                                    ElecReformEIRChiller(EIRChillerNum).Name);
                SetupOutputVariable("Chiller Total Recovered Heat Energy",
                                    OutputProcessor::Unit::J,
                                    ElecReformEIRChiller(EIRChillerNum).EnergyHeatRecovery,
                                    "System",
                                    "Sum",
                                    ElecReformEIRChiller(EIRChillerNum).Name,
                                    _,
                                    "ENERGYTRANSFER",
                                    "HEATRECOVERY",
                                    _,
                                    "Plant");
                SetupOutputVariable("Chiller Heat Recovery Inlet Temperature",
                                    OutputProcessor::Unit::C,
                                    ElecReformEIRChiller(EIRChillerNum).HeatRecInletTemp,
                                    "System",
                                    "Average",
                                    ElecReformEIRChiller(EIRChillerNum).Name);
                SetupOutputVariable("Chiller Heat Recovery Outlet Temperature",
                                    OutputProcessor::Unit::C,
                                    ElecReformEIRChiller(EIRChillerNum).HeatRecOutletTemp,
                                    "System",
                                    "Average",
                                    ElecReformEIRChiller(EIRChillerNum).Name);
                SetupOutputVariable("Chiller Heat Recovery Mass Flow Rate",
                                    OutputProcessor::Unit::kg_s,
                                    ElecReformEIRChiller(EIRChillerNum).HeatRecMassFlow,
                                    "System",
                                    "Average",
                                    ElecReformEIRChiller(EIRChillerNum).Name);
                SetupOutputVariable("Chiller Effective Heat Rejection Temperature",
                                    OutputProcessor::Unit::C,
                                    ElecReformEIRChiller(EIRChillerNum).ChillerCondAvgTemp,
                                    "System",
                                    "Average",
                                    ElecReformEIRChiller(EIRChillerNum).Name);
            }

            if (DataGlobals::AnyEnergyManagementSystemInModel) {
                SetupEMSInternalVariable(
                    "Chiller Nominal Capacity", ElecReformEIRChiller(EIRChillerNum).Name, "[W]", ElecReformEIRChiller(EIRChillerNum).RefCap);
            }
        }
    }

    void InitElecReformEIRChiller(int const EIRChillNum, // Number of the current electric EIR chiller being simulated
                                  bool const RunFlag,    // TRUE when chiller operating
                                  Real64 const MyLoad    // Current load put on chiller
    )
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Lixing Gu, FSEC
        //       DATE WRITTEN   July 2006
        //       MODIFIED       na
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        //  This subroutine is for initializations of the Reformulated Electric EIR Chiller variables

        // METHODOLOGY EMPLOYED:
        //  Uses the status flags to trigger initializations.

        static std::string const RoutineName("InitElecReformEIRChiller");

        // Init more variables
        if (ElecReformEIRChiller(EIRChillNum).MyInitFlag) {
            // Locate the chillers on the plant loops for later usage
            bool errFlag = false;
            PlantUtilities::ScanPlantLoopsForObject(ElecReformEIRChiller(EIRChillNum).Name,
                                    DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                    ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                    ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                    ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                    ElecReformEIRChiller(EIRChillNum).CWCompNum,
                                    errFlag,
                                    ElecReformEIRChiller(EIRChillNum).TempLowLimitEvapOut,
                                    _,
                                    _,
                                    ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                                    _);
            if (ElecReformEIRChiller(EIRChillNum).CondenserType != AirCooled) {
                PlantUtilities::ScanPlantLoopsForObject(ElecReformEIRChiller(EIRChillNum).Name,
                                        DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                        ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                        ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                        ElecReformEIRChiller(EIRChillNum).CDBranchNum,
                                        ElecReformEIRChiller(EIRChillNum).CDCompNum,
                                        errFlag,
                                        _,
                                        _,
                                        _,
                                        ElecReformEIRChiller(EIRChillNum).CondInletNodeNum,
                                        _);
                PlantUtilities::InterConnectTwoPlantLoopSides(ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                              ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                              DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                              true);
            }
            if (ElecReformEIRChiller(EIRChillNum).HeatRecActive) {
                PlantUtilities::ScanPlantLoopsForObject(ElecReformEIRChiller(EIRChillNum).Name,
                                        DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                        ElecReformEIRChiller(EIRChillNum).HRLoopNum,
                                        ElecReformEIRChiller(EIRChillNum).HRLoopSideNum,
                                        ElecReformEIRChiller(EIRChillNum).HRBranchNum,
                                        ElecReformEIRChiller(EIRChillNum).HRCompNum,
                                        errFlag,
                                        _,
                                        _,
                                        _,
                                        ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum,
                                        _);
                PlantUtilities::InterConnectTwoPlantLoopSides(ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                              ElecReformEIRChiller(EIRChillNum).HRLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).HRLoopSideNum,
                                              DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                              true);
            }

            if ((ElecReformEIRChiller(EIRChillNum).CondenserType != AirCooled) && (ElecReformEIRChiller(EIRChillNum).HeatRecActive)) {
                PlantUtilities::InterConnectTwoPlantLoopSides(ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                              ElecReformEIRChiller(EIRChillNum).HRLoopNum,
                                              ElecReformEIRChiller(EIRChillNum).HRLoopSideNum,
                                              DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                              false);
            }

            if (errFlag) {
                ShowFatalError("InitElecReformEIRChiller: Program terminated due to previous condition(s).");
            }

            if (ElecReformEIRChiller(EIRChillNum).FlowMode == ConstantFlow) {
                // reset flow priority
                DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum)
                    .LoopSide(ElecReformEIRChiller(EIRChillNum).CWLoopSideNum)
                    .Branch(ElecReformEIRChiller(EIRChillNum).CWBranchNum)
                    .Comp(ElecReformEIRChiller(EIRChillNum).CWCompNum)
                    .FlowPriority = DataPlant::LoopFlowStatus_NeedyIfLoopOn;
            }

            if (ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) {
                // reset flow priority
                DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum)
                    .LoopSide(ElecReformEIRChiller(EIRChillNum).CWLoopSideNum)
                    .Branch(ElecReformEIRChiller(EIRChillNum).CWBranchNum)
                    .Comp(ElecReformEIRChiller(EIRChillNum).CWCompNum)
                    .FlowPriority = DataPlant::LoopFlowStatus_NeedyIfLoopOn;
                // check if setpoint on outlet node
                if ((DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint == DataLoopNode::SensedNodeFlagValue) &&
                    (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi == DataLoopNode::SensedNodeFlagValue)) {
                    if (!DataGlobals::AnyEnergyManagementSystemInModel) {
                        if (!ElecReformEIRChiller(EIRChillNum).ModulatedFlowErrDone) {
                            ShowWarningError("Missing temperature setpoint for LeavingSetpointModulated mode chiller named " +
                                             ElecReformEIRChiller(EIRChillNum).Name);
                            ShowContinueError(
                                "  A temperature setpoint is needed at the outlet node of a chiller in variable flow mode, use a SetpointManager");
                            ShowContinueError("  The overall loop setpoint will be assumed for chiller. The simulation continues ... ");
                            ElecReformEIRChiller(EIRChillNum).ModulatedFlowErrDone = true;
                        }
                    } else {
                        // need call to EMS to check node
                        bool fatalError = false; // but not really fatal yet, but should be.
                        EMSManager::CheckIfNodeSetPointManagedByEMS(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum, EMSManager::iTemperatureSetPoint, fatalError);
                        if (fatalError) {
                            if (!ElecReformEIRChiller(EIRChillNum).ModulatedFlowErrDone) {
                                ShowWarningError("Missing temperature setpoint for LeavingSetpointModulated mode chiller named " +
                                                 ElecReformEIRChiller(EIRChillNum).Name);
                                ShowContinueError(
                                    "  A temperature setpoint is needed at the outlet node of a chiller evaporator in variable flow mode");
                                ShowContinueError("  use a Setpoint Manager to establish a setpoint at the chiller evaporator outlet node ");
                                ShowContinueError("  or use an EMS actuator to establish a setpoint at the outlet node ");
                                ShowContinueError("  The overall loop setpoint will be assumed for chiller. The simulation continues ... ");
                                ElecReformEIRChiller(EIRChillNum).ModulatedFlowErrDone = true;
                            }
                        }
                    }
                    ElecReformEIRChiller(EIRChillNum).ModulatedFlowSetToLoop = true;
                    DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint =
                        DataLoopNode::Node(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).TempSetPointNodeNum).TempSetPoint;
                    DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi =
                        DataLoopNode::Node(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).TempSetPointNodeNum).TempSetPointHi;
                }
            }
            ElecReformEIRChiller(EIRChillNum).MyInitFlag = false;
        }

        if (ElecReformEIRChiller(EIRChillNum).MyEnvrnFlag && DataGlobals::BeginEnvrnFlag && (DataPlant::PlantFirstSizesOkayToFinalize)) {

            Real64 rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidName,
                                   DataGlobals::CWInitConvTemp,
                                   DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidIndex,
                                   RoutineName);

            ElecReformEIRChiller(EIRChillNum).EvapMassFlowRateMax = ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate * rho;

            PlantUtilities::InitComponentNodes(0.0,
                               ElecReformEIRChiller(EIRChillNum).EvapMassFlowRateMax,
                               ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                               ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                               ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                               ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                               ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                               ElecReformEIRChiller(EIRChillNum).CWCompNum);

            if (ElecReformEIRChiller(EIRChillNum).CondenserType == WaterCooled) {

                rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                       ElecReformEIRChiller(EIRChillNum).TempRefCondIn,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                       RoutineName);
                ElecReformEIRChiller(EIRChillNum).CondMassFlowRateMax = rho * ElecReformEIRChiller(EIRChillNum).CondVolFlowRate;
                PlantUtilities::InitComponentNodes(0.0,
                                   ElecReformEIRChiller(EIRChillNum).CondMassFlowRateMax,
                                   ElecReformEIRChiller(EIRChillNum).CondInletNodeNum,
                                   ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum,
                                   ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                   ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                   ElecReformEIRChiller(EIRChillNum).CDBranchNum,
                                   ElecReformEIRChiller(EIRChillNum).CDCompNum);
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).Temp = ElecReformEIRChiller(EIRChillNum).TempRefCondIn;
            } else { // air or evap air condenser
                // Initialize maximum available condenser flow rate
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate = ElecReformEIRChiller(EIRChillNum).CondVolFlowRate *
                        Psychrometrics::PsyRhoAirFnPbTdbW(DataEnvironment::StdBaroPress, ElecReformEIRChiller(EIRChillNum).TempRefCondIn, 0.0, RoutineName);
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum).MassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRateMaxAvail = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRateMax = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum).MassFlowRateMax = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRateMinAvail = 0.0;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRateMin = 0.0;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum).MassFlowRateMinAvail = 0.0;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum).MassFlowRateMin = 0.0;
                DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).Temp = ElecReformEIRChiller(EIRChillNum).TempRefCondIn;
            }

            if (ElecReformEIRChiller(EIRChillNum).HeatRecActive) {
                rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).HRLoopNum).FluidName,
                                       DataGlobals::HWInitConvTemp,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).HRLoopNum).FluidIndex,
                                       RoutineName);
                ElecReformEIRChiller(EIRChillNum).DesignHeatRecMassFlowRate = rho * ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRate;
                PlantUtilities::InitComponentNodes(0.0,
                                   ElecReformEIRChiller(EIRChillNum).DesignHeatRecMassFlowRate,
                                   ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum,
                                   ElecReformEIRChiller(EIRChillNum).HeatRecOutletNodeNum,
                                   ElecReformEIRChiller(EIRChillNum).HRLoopNum,
                                   ElecReformEIRChiller(EIRChillNum).HRLoopSideNum,
                                   ElecReformEIRChiller(EIRChillNum).HRBranchNum,
                                   ElecReformEIRChiller(EIRChillNum).HRCompNum);
                // overall capacity limit
                ElecReformEIRChiller(EIRChillNum).HeatRecMaxCapacityLimit =
                    ElecReformEIRChiller(EIRChillNum).HeatRecCapacityFraction *
                    (ElecReformEIRChiller(EIRChillNum).RefCap + ElecReformEIRChiller(EIRChillNum).RefCap / ElecReformEIRChiller(EIRChillNum).RefCOP);
            }

            ElecReformEIRChiller(EIRChillNum).MyEnvrnFlag = false;
        }
        if (!DataGlobals::BeginEnvrnFlag) {
            ElecReformEIRChiller(EIRChillNum).MyEnvrnFlag = true;
        }

        if ((ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) && ElecReformEIRChiller(EIRChillNum).ModulatedFlowSetToLoop) {
            // fix for clumsy old input that worked because loop setpoint was spread.
            //  could be removed with transition, testing , model change, period of being obsolete.
            DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint =
                DataLoopNode::Node(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).TempSetPointNodeNum).TempSetPoint;
            DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi =
                DataLoopNode::Node(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).TempSetPointNodeNum).TempSetPointHi;
        }

        Real64 mdot;
        Real64 mdotCond;
        if ((std::abs(MyLoad) > 0.0) && RunFlag) {
            mdot = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRateMax;
            mdotCond = ElecReformEIRChiller(EIRChillNum).CondMassFlowRateMax;
        } else {
            mdot = 0.0;
            mdotCond = 0.0;
        }

        PlantUtilities::SetComponentFlowRate(mdot,
                             ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                             ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                             ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                             ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                             ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                             ElecReformEIRChiller(EIRChillNum).CWCompNum);

        if (ElecReformEIRChiller(EIRChillNum).CondenserType == WaterCooled) {
            PlantUtilities::SetComponentFlowRate(mdotCond,
                                 ElecReformEIRChiller(EIRChillNum).CondInletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                 ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                 ElecReformEIRChiller(EIRChillNum).CDBranchNum,
                                 ElecReformEIRChiller(EIRChillNum).CDCompNum);
        }
        // Initialize heat recovery flow rates at node
        if (ElecReformEIRChiller(EIRChillNum).HeatRecActive) {
            int LoopNum = ElecReformEIRChiller(EIRChillNum).HRLoopNum;
            int LoopSideNum = ElecReformEIRChiller(EIRChillNum).HRLoopSideNum;
            int BranchIndex = ElecReformEIRChiller(EIRChillNum).HRBranchNum;
            int CompIndex = ElecReformEIRChiller(EIRChillNum).HRCompNum;

            // check if inlet limit active and if exceeded.
            bool HeatRecRunFlag;
            if (ElecReformEIRChiller(EIRChillNum).HeatRecInletLimitSchedNum > 0) {
                Real64 HeatRecHighInletLimit = ScheduleManager::GetCurrentScheduleValue(ElecReformEIRChiller(EIRChillNum).HeatRecInletLimitSchedNum);
                if (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum).Temp > HeatRecHighInletLimit) { // shut down heat recovery
                    HeatRecRunFlag = false;
                } else {
                    HeatRecRunFlag = RunFlag;
                }
            } else {
                HeatRecRunFlag = RunFlag;
            }

            if (HeatRecRunFlag) {
                mdot = ElecReformEIRChiller(EIRChillNum).DesignHeatRecMassFlowRate;
            } else {
                mdot = 0.0;
            }

            PlantUtilities::SetComponentFlowRate(mdot, ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum, ElecReformEIRChiller(EIRChillNum).HeatRecOutletNodeNum, LoopNum, LoopSideNum, BranchIndex, CompIndex);
        }
    }

    void SizeElecReformEIRChiller(int const EIRChillNum)
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Richard Raustad, FSEC
        //       DATE WRITTEN   June 2004
        //       MODIFIED       July 2006, L. Gu, modified for reformulated EIR chiller
        //                      November 2013 Daeho Kang, add component sizing table entries
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        //  This subroutine is for sizing Reformulated Electric EIR Chiller Components for which capacities and flow rates
        //  have not been specified in the input.

        // METHODOLOGY EMPLOYED:
        //  Obtains evaporator flow rate from the plant sizing array. Calculates reference capacity from
        //  the evaporator flow rate and the chilled water loop design delta T. The condenser flow rate
        //  is calculated from the reference capacity, the COP, and the condenser loop design delta T.

        static std::string const RoutineName("SizeElecReformEIRChiller");

        bool ErrorsFound(false);           // If errors detected in input

        // Formats
        static ObjexxFCL::gio::Fmt Format_530("('Cond Temp (C) = ',11(F7.2))");
        static ObjexxFCL::gio::Fmt Format_531("('Curve Output  = ',11(F7.2))");

        Real64 tmpNomCap = ElecReformEIRChiller(EIRChillNum).RefCap;
        Real64 tmpEvapVolFlowRate = ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate;
        Real64 tmpCondVolFlowRate = ElecReformEIRChiller(EIRChillNum).CondVolFlowRate;

        int PltSizCondNum(0);              // Plant Sizing index for condenser loop
        if (ElecReformEIRChiller(EIRChillNum).CondenserType == WaterCooled) {
            PltSizCondNum = DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).PlantSizNum;
        }

        // find the appropriate Plant Sizing object
        int PltSizNum = DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).PlantSizNum;

        if (PltSizNum > 0) {
            if (DataSizing::PlantSizData(PltSizNum).DesVolFlowRate >= DataHVACGlobals::SmallWaterVolFlow) {
                tmpEvapVolFlowRate = DataSizing::PlantSizData(PltSizNum).DesVolFlowRate * ElecReformEIRChiller(EIRChillNum).SizFac;
            } else {
                if (ElecReformEIRChiller(EIRChillNum).EvapVolFlowRateWasAutoSized) tmpEvapVolFlowRate = 0.0;
            }
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (ElecReformEIRChiller(EIRChillNum).EvapVolFlowRateWasAutoSized) {
                    ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate = tmpEvapVolFlowRate;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Design Size Reference Chilled Water Flow Rate [m3/s]",
                                           tmpEvapVolFlowRate);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Initial Design Size Reference Chilled Water Flow Rate [m3/s]",
                                           tmpEvapVolFlowRate);
                    }
                } else { // Hard-size with sizing data
                    if (ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate > 0.0 && tmpEvapVolFlowRate > 0.0) {
                        Real64 EvapVolFlowRateUser = ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate;
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                               ElecReformEIRChiller(EIRChillNum).Name,
                                               "Design Size Reference Chilled Water Flow Rate [m3/s]",
                                               tmpEvapVolFlowRate,
                                               "User-Specified Reference Chilled Water Flow Rate [m3/s]",
                                               EvapVolFlowRateUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpEvapVolFlowRate - EvapVolFlowRateUser) / EvapVolFlowRateUser) > DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("SizeChillerElectricReformulatedEIR: Potential issue with equipment sizing for " +
                                                ElecReformEIRChiller(EIRChillNum).Name);
                                    ShowContinueError("User-Specified Reference Chilled Water Flow Rate of " +
                                                      General::RoundSigDigits(EvapVolFlowRateUser, 5) + " [m3/s]");
                                    ShowContinueError("differs from Design Size Reference Chilled Water Flow Rate of " +
                                                      General::RoundSigDigits(tmpEvapVolFlowRate, 5) + " [m3/s]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                        tmpEvapVolFlowRate = EvapVolFlowRateUser;
                    }
                }
            }
        } else {
            if (ElecReformEIRChiller(EIRChillNum).EvapVolFlowRateWasAutoSized && DataPlant::PlantFirstSizesOkayToFinalize) {
                ShowSevereError("Autosizing of Reformulated Electric Chiller evap flow rate requires a loop Sizing:Plant object");
                ShowContinueError("Occurs in Reformulated Electric Chiller object=" + ElecReformEIRChiller(EIRChillNum).Name);
                ErrorsFound = true;
            }
            if (!ElecReformEIRChiller(EIRChillNum).EvapVolFlowRateWasAutoSized && DataPlant::PlantFinalSizesOkayToReport &&
                (ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate > 0.0)) { // Hard-size with sizing data
                ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                   ElecReformEIRChiller(EIRChillNum).Name,
                                   "User-Specified Reference Chilled Water Flow Rate [m3/s]",
                                   ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate);
            }
        }

        PlantUtilities::RegisterPlantCompDesignFlow(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum, tmpEvapVolFlowRate);

        if (PltSizNum > 0) {
            if (DataSizing::PlantSizData(PltSizNum).DesVolFlowRate >= DataHVACGlobals::SmallWaterVolFlow) {
                Real64 SizingEvapOutletTemp;       // Plant Sizing outlet temperature for CurLoopNum [C]
                Real64 SizingCondOutletTemp;       // Plant Sizing outlet temperature for condenser loop [C]
                if (PltSizCondNum > 0 && PltSizNum > 0) {
                    SizingEvapOutletTemp = DataSizing::PlantSizData(PltSizNum).ExitTemp;
                    SizingCondOutletTemp = DataSizing::PlantSizData(PltSizCondNum).ExitTemp + DataSizing::PlantSizData(PltSizCondNum).DeltaT;
                } else {
                    SizingEvapOutletTemp = ElecReformEIRChiller(EIRChillNum).TempRefEvapOut;
                    SizingCondOutletTemp = ElecReformEIRChiller(EIRChillNum).TempRefCondOut;
                }
                Real64 Cp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidName,
                                           DataGlobals::CWInitConvTemp,
                                           DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidIndex,
                                           RoutineName);
                Real64 rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidName,
                                       DataGlobals::CWInitConvTemp,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidIndex,
                                       RoutineName);
                Real64 RefCapFT = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex, SizingEvapOutletTemp, SizingCondOutletTemp);
                tmpNomCap = (Cp * rho * DataSizing::PlantSizData(PltSizNum).DeltaT * tmpEvapVolFlowRate) / RefCapFT;
            } else {
                if (ElecReformEIRChiller(EIRChillNum).RefCapWasAutoSized) tmpNomCap = 0.0;
            }
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (ElecReformEIRChiller(EIRChillNum).RefCapWasAutoSized) {
                    ElecReformEIRChiller(EIRChillNum).RefCap = tmpNomCap;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Design Size Reference Capacity [W]",
                                           tmpNomCap);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Initial Design Size Reference Capacity [W]",
                                           tmpNomCap);
                    }
                } else {
                    if (ElecReformEIRChiller(EIRChillNum).RefCap > 0.0 && tmpNomCap > 0.0) {
                        Real64 RefCapUser = ElecReformEIRChiller(EIRChillNum).RefCap;
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                               ElecReformEIRChiller(EIRChillNum).Name,
                                               "Design Size Reference Capacity [W]",
                                               tmpNomCap,
                                               "User-Specified Reference Capacity [W]",
                                               RefCapUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpNomCap - RefCapUser) / RefCapUser) > DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("Size:ChillerElectricReformulatedEIR: Potential issue with equipment sizing for " +
                                                ElecReformEIRChiller(EIRChillNum).Name);
                                    ShowContinueError("User-Specified Reference Capacity of " + General::RoundSigDigits(RefCapUser, 2) + " [W]");
                                    ShowContinueError("differs from Design Size Reference Capacity of " + General::RoundSigDigits(tmpNomCap, 2) + " [W]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                        tmpNomCap = RefCapUser;
                    }
                }
            }
        } else {
            if (ElecReformEIRChiller(EIRChillNum).RefCapWasAutoSized && DataPlant::PlantFirstSizesOkayToFinalize) {
                ShowSevereError("Autosizing of Reformulated Electric Chiller reference capacity requires a loop Sizing:Plant object");
                ShowContinueError("Occurs in Reformulated Electric Chiller object=" + ElecReformEIRChiller(EIRChillNum).Name);
                ErrorsFound = true;
            }
            if (!ElecReformEIRChiller(EIRChillNum).RefCapWasAutoSized && DataPlant::PlantFinalSizesOkayToReport &&
                (ElecReformEIRChiller(EIRChillNum).RefCap > 0.0)) {
                ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                   ElecReformEIRChiller(EIRChillNum).Name,
                                   "User-Specified Reference Capacity [W]",
                                   ElecReformEIRChiller(EIRChillNum).RefCap);
            }
        }

        if (PltSizCondNum > 0 && PltSizNum > 0) {
            if (DataSizing::PlantSizData(PltSizNum).DesVolFlowRate >= DataHVACGlobals::SmallWaterVolFlow && tmpNomCap > 0.0) {
                Real64 rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                       DataGlobals::CWInitConvTemp,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                       RoutineName);
                Real64 Cp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                           ElecReformEIRChiller(EIRChillNum).TempRefCondIn,
                                           DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                           RoutineName);
                tmpCondVolFlowRate =
                    tmpNomCap *
                    (1.0 + (1.0 / ElecReformEIRChiller(EIRChillNum).RefCOP) * ElecReformEIRChiller(EIRChillNum).CompPowerToCondenserFrac) /
                    (DataSizing::PlantSizData(PltSizCondNum).DeltaT * Cp * rho);
                // IF (DataPlant::PlantFirstSizesOkayToFinalize) ElecReformEIRChiller(EIRChillNum)%CondVolFlowRate = tmpCondVolFlowRate
            } else {
                if (ElecReformEIRChiller(EIRChillNum).CondVolFlowRateWasAutoSized) tmpCondVolFlowRate = 0.0;
                // IF (DataPlant::PlantFirstSizesOkayToFinalize) ElecReformEIRChiller(EIRChillNum)%CondVolFlowRate = tmpCondVolFlowRate
            }
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (ElecReformEIRChiller(EIRChillNum).CondVolFlowRateWasAutoSized) {
                    ElecReformEIRChiller(EIRChillNum).CondVolFlowRate = tmpCondVolFlowRate;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Design Size Reference Condenser Water Flow Rate [m3/s]",
                                           tmpCondVolFlowRate);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Initial Design Size Reference Condenser Water Flow Rate [m3/s]",
                                           tmpCondVolFlowRate);
                    }
                } else {
                    if (ElecReformEIRChiller(EIRChillNum).CondVolFlowRate > 0.0 && tmpCondVolFlowRate > 0.0) {
                        Real64 CondVolFlowRateUser = ElecReformEIRChiller(EIRChillNum).CondVolFlowRate;
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                               ElecReformEIRChiller(EIRChillNum).Name,
                                               "Design Size Reference Condenser Water Flow Rate [m3/s]",
                                               tmpCondVolFlowRate,
                                               "User-Specified Reference Condenser Water Flow Rate [m3/s]",
                                               CondVolFlowRateUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpCondVolFlowRate - CondVolFlowRateUser) / CondVolFlowRateUser) > DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("Size:ChillerElectricReformulatedEIR: Potential issue with equipment sizing for " +
                                                ElecReformEIRChiller(EIRChillNum).Name);
                                    ShowContinueError("User-Specified Reference Condenser Water Flow Rate of " +
                                                      General::RoundSigDigits(CondVolFlowRateUser, 5) + " [m3/s]");
                                    ShowContinueError("differs from Design Size Reference Condenser Water Flow Rate of " +
                                                      General::RoundSigDigits(tmpCondVolFlowRate, 5) + " [m3/s]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                        tmpCondVolFlowRate = CondVolFlowRateUser;
                    }
                }
            }
        } else {
            if (ElecReformEIRChiller(EIRChillNum).CondVolFlowRateWasAutoSized && DataPlant::PlantFirstSizesOkayToFinalize) {
                ShowSevereError("Autosizing of Reformulated Electric EIR Chiller condenser flow rate requires a condenser");
                ShowContinueError("loop Sizing:Plant object");
                ShowContinueError("Occurs in Reformulated Electric EIR Chiller object=" + ElecReformEIRChiller(EIRChillNum).Name);
                ErrorsFound = true;
            }
            if (!ElecReformEIRChiller(EIRChillNum).CondVolFlowRateWasAutoSized && DataPlant::PlantFinalSizesOkayToReport &&
                (ElecReformEIRChiller(EIRChillNum).CondVolFlowRate > 0.0)) {
                ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                   ElecReformEIRChiller(EIRChillNum).Name,
                                   "User-Specified Reference Condenser Water Flow Rate [m3/s]",
                                   ElecReformEIRChiller(EIRChillNum).CondVolFlowRate);
            }
        }

        // save the reference condenser water volumetric flow rate for use by the condenser water loop sizing algorithms
        PlantUtilities::RegisterPlantCompDesignFlow(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum, tmpCondVolFlowRate);

        if (ElecReformEIRChiller(EIRChillNum).HeatRecActive) {
            Real64 tmpHeatRecVolFlowRate = tmpCondVolFlowRate * ElecReformEIRChiller(EIRChillNum).HeatRecCapacityFraction;
            if (!ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRateWasAutoSized)
                tmpHeatRecVolFlowRate = ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRate;
            if (DataPlant::PlantFirstSizesOkayToFinalize) {
                if (ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRateWasAutoSized) {
                    ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRate = tmpHeatRecVolFlowRate;
                    if (DataPlant::PlantFinalSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Design Size Design Heat Recovery Fluid Flow Rate [m3/s]",
                                           tmpHeatRecVolFlowRate);
                    }
                    if (DataPlant::PlantFirstSizesOkayToReport) {
                        ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                           ElecReformEIRChiller(EIRChillNum).Name,
                                           "Initial Design Size Design Heat Recovery Fluid Flow Rate [m3/s]",
                                           tmpHeatRecVolFlowRate);
                    }
                } else {
                    if (ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRate > 0.0 && tmpHeatRecVolFlowRate > 0.0) {
                        Real64 DesignHeatRecVolFlowRateUser = ElecReformEIRChiller(EIRChillNum).DesignHeatRecVolFlowRate;
                        if (DataPlant::PlantFinalSizesOkayToReport) {
                            ReportSizingManager::ReportSizingOutput("Chiller:Electric:ReformulatedEIR",
                                               ElecReformEIRChiller(EIRChillNum).Name,
                                               "Design Size Design Heat Recovery Fluid Flow Rate [m3/s]",
                                               tmpHeatRecVolFlowRate,
                                               "User-Specified Design Heat Recovery Fluid Flow Rate [m3/s]",
                                               DesignHeatRecVolFlowRateUser);
                            if (DataGlobals::DisplayExtraWarnings) {
                                if ((std::abs(tmpHeatRecVolFlowRate - DesignHeatRecVolFlowRateUser) / DesignHeatRecVolFlowRateUser) >
                                    DataSizing::AutoVsHardSizingThreshold) {
                                    ShowMessage("Size:ChillerElectricReformulatedEIR: Potential issue with equipment sizing for " +
                                                ElecReformEIRChiller(EIRChillNum).Name);
                                    ShowContinueError("User-Specified Design Heat Recovery Fluid Flow Rate of " +
                                                      General::RoundSigDigits(DesignHeatRecVolFlowRateUser, 5) + " [m3/s]");
                                    ShowContinueError("differs from Design Size Design Heat Recovery Fluid Flow Rate of " +
                                                      General::RoundSigDigits(tmpHeatRecVolFlowRate, 5) + " [m3/s]");
                                    ShowContinueError("This may, or may not, indicate mismatched component sizes.");
                                    ShowContinueError("Verify that the value entered is intended and is consistent with other components.");
                                }
                            }
                        }
                        tmpHeatRecVolFlowRate = DesignHeatRecVolFlowRateUser;
                    }
                }
            }
            // save the reference heat recovery fluid volumetric flow rate
            PlantUtilities::RegisterPlantCompDesignFlow(ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum, tmpHeatRecVolFlowRate);
        }

        std::string equipName;             // Name of chiller
        if (DataPlant::PlantFinalSizesOkayToReport) {
            if (ElecReformEIRChiller(EIRChillNum).MySizeFlag) {
                Real64 IPLV;
                StandardRatings::CalcChillerIPLV(ElecReformEIRChiller(EIRChillNum).Name,
                                DataPlant::TypeOf_Chiller_ElectricReformEIR,
                                ElecReformEIRChiller(EIRChillNum).RefCap,
                                ElecReformEIRChiller(EIRChillNum).RefCOP,
                                ElecReformEIRChiller(EIRChillNum).CondenserType,
                                ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex,
                                ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex,
                                ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex,
                                ElecReformEIRChiller(EIRChillNum).MinUnloadRat,
                                IPLV,
                                ElecReformEIRChiller(EIRChillNum).EvapVolFlowRate,
                                ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                ElecReformEIRChiller(EIRChillNum).CompPowerToCondenserFrac);
                ElecReformEIRChiller(EIRChillNum).MySizeFlag = false;
            }
            // create predefined report
            equipName = ElecReformEIRChiller(EIRChillNum).Name;
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechType, equipName, "Chiller:Electric:ReformulatedEIR");
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechNomEff, equipName, ElecReformEIRChiller(EIRChillNum).RefCOP);
            OutputReportPredefined::PreDefTableEntry(OutputReportPredefined::pdchMechNomCap, equipName, ElecReformEIRChiller(EIRChillNum).RefCap);
        }

        // Only check performance curves if Capacity and volumetric flow rate are greater than 0
        if (ElecReformEIRChiller(EIRChillNum).RefCap > 0.0 && ElecReformEIRChiller(EIRChillNum).CondVolFlowRate > 0.0) {
            //   Check the CAP-FT, EIR-FT, and PLR curves at reference conditions and warn user if different from 1.0 by more than +-10%
            if (ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex > 0) {
                Real64 CurveVal = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex,
                                      ElecReformEIRChiller(EIRChillNum).TempRefEvapOut,
                                      ElecReformEIRChiller(EIRChillNum).TempRefCondOut);
                if (CurveVal > 1.10 || CurveVal < 0.90) {
                    ShowWarningError("Capacity ratio as a function of temperature curve output is not equal to 1.0");
                    ShowContinueError("(+ or - 10%) at reference conditions for Chiller:Electric:ReformulatedEIR = " + equipName);
                    ShowContinueError("Curve output at reference conditions = " + General::TrimSigDigits(CurveVal, 3));
                }
                CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex,
                                     ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMax,
                                     ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMax);
            }

            if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex > 0) {
                Real64 CurveVal = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex,
                                      ElecReformEIRChiller(EIRChillNum).TempRefEvapOut,
                                      ElecReformEIRChiller(EIRChillNum).TempRefCondOut);
                if (CurveVal > 1.10 || CurveVal < 0.90) {
                    ShowWarningError("Energy input ratio as a function of temperature curve output is not equal to 1.0");
                    ShowContinueError("(+ or - 10%) at reference conditions for Chiller:Electric:ReformulatedEIR = " + equipName);
                    ShowContinueError("Curve output at reference conditions = " + General::TrimSigDigits(CurveVal, 3));
                }
                CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMax,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMax);
            }

            if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex > 0) {
                Real64 CurveVal(0.0);                   // Used to verify EIR-FT/CAP-FT curves = 1 at reference conditions
                if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                    CurveVal = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, ElecReformEIRChiller(EIRChillNum).TempRefCondOut, 1.0);
                } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) {
                    CurveVal = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, 1.0, 1.0, 0.0); // zrp_Aug2014
                }
                if (CurveVal > 1.10 || CurveVal < 0.90) {
                    ShowWarningError("Energy input ratio as a function of part-load ratio curve output is not equal to 1.0");
                    ShowContinueError("(+ or - 10%) at reference conditions for Chiller:Electric:ReformulatedEIR = " + equipName);
                    ShowContinueError("Curve output at reference conditions = " + General::TrimSigDigits(CurveVal, 3));
                }

                if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                    CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMin,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMax,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax);
                } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) { // zrp_Aug2014
                    CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex,
                                         ElecReformEIRChiller(EIRChillNum).ChillerLiftNomMin,
                                         ElecReformEIRChiller(EIRChillNum).ChillerLiftNomMax,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin,
                                         ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax,
                                         ElecReformEIRChiller(EIRChillNum).ChillerTdevNomMin,
                                         ElecReformEIRChiller(EIRChillNum).ChillerTdevNomMax);
                }

                if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin < 0 ||
                    ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin >= ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax ||
                    ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin > 1) {
                    ShowSevereError("Invalid minimum value of PLR = " + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin, 3) +
                                    " in bicubic curve = " + ElecReformEIRChiller(EIRChillNum).EIRFPLRName + " which is used");
                    ShowContinueError("by Chiller:Electric:ReformulatedEIR = " + equipName + '.');
                    ShowContinueError("The minimum value of PLR [y] must be from zero to 1, and less than the maximum value of PLR.");
                    ErrorsFound = true;
                }
                if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax > 1.1 ||
                    ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax <= ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin ||
                    ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax < 0) {
                    ShowSevereError("Invalid maximum value of PLR = " + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax, 3) +
                                    " in bicubic curve = " + ElecReformEIRChiller(EIRChillNum).EIRFPLRName + " which is used");
                    ShowContinueError("by Chiller:Electric:ReformulatedEIR = " + equipName + '.');
                    ShowContinueError("The maximum value of PLR [y] must be from zero to 1.1, and greater than the minimum value of PLR.");
                    ErrorsFound = true;
                }
                //     calculate the condenser outlet temp proportional to PLR and test the EIRFPLR curve output for negative numbers.
            }

            //  Initialize condenser reference inlet temperature (not a user input)
            Real64 Density = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                       ElecReformEIRChiller(EIRChillNum).TempRefCondOut,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                       RoutineName);

            Real64 SpecificHeat = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                                 ElecReformEIRChiller(EIRChillNum).TempRefCondOut,
                                                 DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                                 RoutineName);
            Real64 CondenserCapacity = ElecReformEIRChiller(EIRChillNum).RefCap *
                                (1.0 + (1.0 / ElecReformEIRChiller(EIRChillNum).RefCOP) * ElecReformEIRChiller(EIRChillNum).CompPowerToCondenserFrac);
            Real64 DeltaTCond = (CondenserCapacity) / (ElecReformEIRChiller(EIRChillNum).CondVolFlowRate * Density * SpecificHeat);
            ElecReformEIRChiller(EIRChillNum).TempRefCondIn = ElecReformEIRChiller(EIRChillNum).TempRefCondOut - DeltaTCond;

            if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                //     Check EIRFPLR curve output. Calculate condenser inlet temp based on reference condenser outlet temp,
                //     chiller capacity, and mass flow rate. Starting with the calculated condenser inlet temp and PLR = 0,
                //     calculate the condenser outlet temp proportional to PLR and test the EIRFPLR curve output for negative numbers.
                bool FoundNegValue = false;
                Array1D<Real64> CurveValArray(11, 0.0); // Used to evaluate EIRFPLR curve objects
                Array1D<Real64> CondTempArray(11, 0.0); // Used to evaluate EIRFPLR curve objects

                if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex > 0) {
                    CondTempArray = 0.0;
                    for (int CurveCheck = 0; CurveCheck <= 10; ++CurveCheck) {
                        Real64 PLRTemp = CurveCheck / 10.0;
                        Real64 CondTemp = ElecReformEIRChiller(EIRChillNum).TempRefCondIn + (DeltaTCond * PLRTemp);
                        CondTemp = min(CondTemp, ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMax);
                        CondTemp = max(CondTemp, ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMin);
                        Real64 CurveValTmp;                // Used to evaluate EIRFPLR curve objects
                        if (PLRTemp < ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin) {
                            CurveValTmp = CurveManager::CurveValue(
                                    ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, CondTemp, ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin);
                        } else {
                            CurveValTmp = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, CondTemp, PLRTemp);
                        }
                        if (CurveValTmp < 0.0) FoundNegValue = true;
                        CurveValArray(CurveCheck + 1) = int(CurveValTmp * 100.0) / 100.0;
                        CondTempArray(CurveCheck + 1) = int(CondTemp * 100.0) / 100.0;
                    }
                }

                //     Output warning message if negative values are found in the EIRFPLR curve output. Results in Fatal error.
                if (FoundNegValue) {
                    ShowWarningError("Energy input to cooing output ratio function of part-load ratio curve shows negative values ");
                    ShowContinueError("for  Chiller:Electric:ReformulatedEIR = " + equipName + '.');
                    ShowContinueError(
                        "EIR as a function of PLR curve output at various part-load ratios and condenser water temperatures shown below:");
                    ShowContinueError("PLR           =    0.00   0.10   0.20   0.30   0.40   0.50   0.60   0.70   0.80   0.90   1.00");
                    std::string StringVar;             // Used for EIRFPLR warning messages
                    ObjexxFCL::gio::write(StringVar, "'Cond Temp(C) = '");
                    for (int CurveValPtr = 1; CurveValPtr <= 11; ++CurveValPtr) {
                        ObjexxFCL::gio::write(StringVar, "(F7.2,$)") << CondTempArray(CurveValPtr);
                    }
                    ObjexxFCL::gio::write(StringVar);
                    ShowContinueError(StringVar);
                    ObjexxFCL::gio::write(StringVar, "'Curve Output = '");
                    for (int CurveValPtr = 1; CurveValPtr <= 11; ++CurveValPtr) {
                        ObjexxFCL::gio::write(StringVar, "(F7.2,$)") << CurveValArray(CurveValPtr);
                    }
                    ObjexxFCL::gio::write(StringVar);
                    ShowContinueError(StringVar);
                    ErrorsFound = true;
                }
            }
        } else { // just get curve min/max values if capacity or cond volume flow rate = 0
            CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex,
                                 ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMin,
                                 ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMax,
                                 ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMin,
                                 ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMax);
            CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex,
                                 ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMin,
                                 ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMax,
                                 ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMin,
                                 ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMax);
            if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMax,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax);
            } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) { // zrp_Aug2014
                CurveManager::GetCurveMinMaxValues(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex,
                                     ElecReformEIRChiller(EIRChillNum).ChillerLiftNomMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerLiftNomMax,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax,
                                     ElecReformEIRChiller(EIRChillNum).ChillerTdevNomMin,
                                     ElecReformEIRChiller(EIRChillNum).ChillerTdevNomMax);
            }
        }

        if (ErrorsFound) {
            ShowFatalError("Preceding sizing errors cause program termination");
        }
    }

    void ControlReformEIRChillerModel(int &EIRChillNum,          // Chiller number
                                      Real64 &MyLoad,            // Operating load [W]
                                      bool const RunFlag,        // TRUE when chiller operating
                                      bool const FirstIteration, // TRUE when first iteration of timestep
                                      int const EquipFlowCtrl    // Flow control mode for the equipment
    )
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Lixing Gu, FSEC
        //       DATE WRITTEN   July 2006
        //       MODIFIED       na
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS SUBROUTINE:
        // Simulate a vapor compression chiller using the reformulated model developed by Mark Hydeman

        // METHODOLOGY EMPLOYED:
        // Use empirical curve fits to model performance at off-design conditions. This subroutine
        // calls Subroutines CalcReformEIRChillerModel and General::SolveRoot to obtain solution.
        // The actual chiller performance calculations are in Subroutine CalcReformEIRChillerModel.

        // REFERENCES:
        // 1. Hydeman, M., P. Sreedharan, N. Webb, and S. Blanc. 2002. "Development and Testing of a Reformulated
        //    Regression-Based Electric Chiller Model". ASHRAE Transactions, HI-02-18-2, Vol 108, Part 2, pp. 1118-1127.

        Real64 const Acc(0.0001); // Accuracy control for General::SolveRoot
        int const MaxIter(500);   // Iteration control for General::SolveRoot

        if (MyLoad >= 0.0 || !RunFlag) {
            CalcReformEIRChillerModel(
                EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).Temp);
        } else {

            //  Find min/max condenser outlet temperature used by curve objects

            // Minimum condenser leaving temperature allowed by CAPFT curve [C]
            Real64 CAPFTYTmin = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMin;

            // Minimum condenser leaving temperature allowed by EIRFT curve [C]
            Real64 Tmin(-99);        // Minimum condenser leaving temperature allowed by curve objects [C]

            Real64 EIRFTYTmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMin;
            if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                // Minimum condenser leaving temperature allowed by EIRFPLR curve [C]
                Real64 EIRFPLRTmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMin;
                Tmin = min(CAPFTYTmin, EIRFTYTmin, EIRFPLRTmin);
            } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) { // zrp_Aug2014
                Tmin = min(CAPFTYTmin, EIRFTYTmin);
            }

            // Maximum condenser leaving temperature allowed by CAPFT curve [C]
            Real64 CAPFTYTmax = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMax;

            Real64 Tmax(-99);        // Maximum condenser leaving temperature allowed by curve objects [C]

            // Maximum condenser leaving temperature allowed by EIRFT curve [C]
            Real64 EIRFTYTmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMax;
            if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
                // Maximum condenser leaving temperature allowed by EIRFPLR curve [C]
                Real64 EIRFPLRTmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMax;
                Tmax = max(CAPFTYTmax, EIRFTYTmax, EIRFPLRTmax);
            } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) { // zrp_Aug2014
                Tmax = max(CAPFTYTmax, EIRFTYTmax);
            }

            //  Check that condenser outlet temperature is within curve object limits prior to calling RegulaFalsi
            CalcReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, Tmin);

            // Condenser outlet temperature when using Tmin as input to CalcReformEIRChillerModel [C]
            Real64 CondTempMin = ElecReformEIRChiller(EIRChillNum).CondOutletTemp;
            CalcReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, Tmax);

            // Condenser outlet temperature when using Tmax as input to CalcReformEIRChillerModel [C]
            Real64 CondTempMax = ElecReformEIRChiller(EIRChillNum).CondOutletTemp;

            if (CondTempMin > Tmin && CondTempMax < Tmax) {

                Array1D<Real64> Par(6);  // Pass parameters for RegulaFalsi solver

                //    Initialize iteration parameters for RegulaFalsi function
                Par(1) = EIRChillNum;
                Par(2) = MyLoad;
                if (RunFlag) {
                    Par(3) = 1.0;
                } else {
                    Par(3) = 0.0;
                }
                if (FirstIteration) {
                    Par(4) = 1.0;
                } else {
                    Par(4) = 0.0;
                }
                // Par(5) = FlowLock !DSU
                Par(6) = EquipFlowCtrl;

                int SolFla;              // Feedback flag from General::SolveRoot
                Real64 FalsiCondOutTemp; // RegulaFalsi condenser outlet temperature result [C]
                General::SolveRoot(Acc, MaxIter, SolFla, FalsiCondOutTemp, CondOutTempResidual, Tmin, Tmax, Par);

                if (SolFla == -1) {
                    if (!DataGlobals::WarmupFlag) {
                        ++ElecReformEIRChiller(EIRChillNum).IterLimitExceededNum;
                        if (ElecReformEIRChiller(EIRChillNum).IterLimitExceededNum == 1) {
                            ShowWarningError(
                                ElecReformEIRChiller(EIRChillNum).Name +
                                ": Iteration limit exceeded calculating condenser outlet temperature and non-converged temperature is used");
                        } else {
                            ShowRecurringWarningErrorAtEnd(ElecReformEIRChiller(EIRChillNum).Name +
                                                               ": Iteration limit exceeded calculating condenser outlet temperature.",
                                                           ElecReformEIRChiller(EIRChillNum).IterLimitErrIndex,
                                                           ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                                                           ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
                        }
                    }
                } else if (SolFla == -2) {
                    if (!DataGlobals::WarmupFlag) {
                        ++ElecReformEIRChiller(EIRChillNum).IterFailed;
                        if (ElecReformEIRChiller(EIRChillNum).IterFailed == 1) {
                            ShowWarningError(ElecReformEIRChiller(EIRChillNum).Name + ": Solution found when calculating condenser outlet "
                                                                                      "temperature. The inlet temperature will used and the "
                                                                                      "simulation continues...");
                            ShowContinueError("Please check minimum and maximum values of x in EIRFPLR Curve " +
                                              ElecReformEIRChiller(EIRChillNum).EIRFPLRName);
                        } else {
                            ShowRecurringWarningErrorAtEnd(ElecReformEIRChiller(EIRChillNum).Name +
                                                               ": Solution is not found in calculating condenser outlet temperature.",
                                                           ElecReformEIRChiller(EIRChillNum).IterFailedIndex,
                                                           ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                                                           ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
                        }
                    }
                    CalcReformEIRChillerModel(
                        EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).Temp);
                }
            } else {
                //    If iteration is not possible, average the min/max condenser outlet temperature and manually determine solution
                CalcReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, (CondTempMin + CondTempMax) / 2.0);
                CalcReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
            }

            //  Call subroutine to evaluate all performance curve min/max values against evaporator/condenser outlet temps and PLR
            CheckMinMaxCurveBoundaries(EIRChillNum, FirstIteration);
        }
    }

    void ReformEIRChillerHeatRecovery(int const EIRChillNum,      // Number of the current electric EIR chiller being simulated
                                      Real64 &QCond,              // Current condenser load [W]
                                      Real64 const CondMassFlow,  // Current condenser mass flow [kg/s]
                                      Real64 const CondInletTemp, // Current condenser inlet temp [C]
                                      Real64 &QHeatRec            // Amount of heat recovered [W]
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          Lixing Gu, FSEC
        //       DATE WRITTEN:    July 2006
        //       MODIFIED:        na

        // PURPOSE OF THIS SUBROUTINE:
        //  Calculate the heat recovered from the chiller condenser
        
        static std::string const RoutineName("EIRChillerHeatRecovery");

        // inlet node to the heat recovery heat exchanger
        Real64 HeatRecInletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum).Temp;
        Real64 HeatRecMassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).HeatRecInletNodeNum).MassFlowRate;

        Real64 CpHeatRec = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).HRLoopNum).FluidName,
                                          HeatRecInletTemp,
                                          DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).HRLoopNum).FluidIndex,
                                          RoutineName);
        Real64 CpCond = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                       CondInletTemp,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                       RoutineName);

        // Before we modify the QCondenser, the total or original value is transferred to QTot
        Real64 QTotal = QCond;

        if (ElecReformEIRChiller(EIRChillNum).HeatRecSetPointNodeNum == 0) { // use original algorithm that blends temps
            Real64 TAvgIn = (HeatRecMassFlowRate * CpHeatRec * HeatRecInletTemp + CondMassFlow * CpCond * CondInletTemp) /
                     (HeatRecMassFlowRate * CpHeatRec + CondMassFlow * CpCond);

            Real64 TAvgOut = QTotal / (HeatRecMassFlowRate * CpHeatRec + CondMassFlow * CpCond) + TAvgIn;

            QHeatRec = HeatRecMassFlowRate * CpHeatRec * (TAvgOut - HeatRecInletTemp);
            QHeatRec = max(QHeatRec, 0.0); // ensure non negative
            // check if heat flow too large for physical size of bundle
            QHeatRec = min(QHeatRec, ElecReformEIRChiller(EIRChillNum).HeatRecMaxCapacityLimit);
        } else { // use new algorithm to meet setpoint
            Real64 THeatRecSetPoint(0.0);

            {
                auto const SELECT_CASE_var(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).HRLoopNum).LoopDemandCalcScheme);
                if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                    THeatRecSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).HeatRecSetPointNodeNum).TempSetPoint;
                } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                    THeatRecSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).HeatRecSetPointNodeNum).TempSetPointHi;
                } else {
                    assert(false);
                }
            }

            Real64 QHeatRecToSetPoint = HeatRecMassFlowRate * CpHeatRec * (THeatRecSetPoint - HeatRecInletTemp);
            QHeatRecToSetPoint = max(QHeatRecToSetPoint, 0.0);
            QHeatRec = min(QTotal, QHeatRecToSetPoint);
            // check if heat flow too large for physical size of bundle
            QHeatRec = min(QHeatRec, ElecReformEIRChiller(EIRChillNum).HeatRecMaxCapacityLimit);
        }

        // check if limit on inlet is present and exceeded.
        if (ElecReformEIRChiller(EIRChillNum).HeatRecInletLimitSchedNum > 0) {
            Real64 HeatRecHighInletLimit = ScheduleManager::GetCurrentScheduleValue(ElecReformEIRChiller(EIRChillNum).HeatRecInletLimitSchedNum);
            if (HeatRecInletTemp > HeatRecHighInletLimit) { // shut down heat recovery
                QHeatRec = 0.0;
            }
        }

        QCond = QTotal - QHeatRec;

        // Calculate a new Heat Recovery Coil Outlet Temp
        if (HeatRecMassFlowRate > 0.0) {
            ElecReformEIRChiller(EIRChillNum).HeatRecOutletTemp = QHeatRec / (HeatRecMassFlowRate * CpHeatRec) + HeatRecInletTemp;
        } else {
            ElecReformEIRChiller(EIRChillNum).HeatRecOutletTemp = HeatRecInletTemp;
        }
    }

    void UpdateReformEIRChillerRecords(Real64 const MyLoad, // Current load [W]
                                       bool const RunFlag,  // TRUE if chiller operating
                                       int const Num        // Chiller number
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          Lixing Gu, FSEC
        //       DATE WRITTEN:    July 2006

        // PURPOSE OF THIS SUBROUTINE:
        //  Reporting

        if (MyLoad >= 0.0 || !RunFlag) { // Chiller not running so pass inlet states to outlet states
            // Set node temperatures
            DataLoopNode::Node(ElecReformEIRChiller(Num).EvapOutletNodeNum).Temp = DataLoopNode::Node(ElecReformEIRChiller(Num).EvapInletNodeNum).Temp;
            DataLoopNode::Node(ElecReformEIRChiller(Num).CondOutletNodeNum).Temp = DataLoopNode::Node(ElecReformEIRChiller(Num).CondInletNodeNum).Temp;

            ElecReformEIRChiller(Num).ChillerPartLoadRatio = 0.0;
            ElecReformEIRChiller(Num).ChillerCyclingRatio = 0.0;
            ElecReformEIRChiller(Num).ChillerFalseLoadRate = 0.0;
            ElecReformEIRChiller(Num).ChillerFalseLoad = 0.0;
            ElecReformEIRChiller(Num).Power = 0.0;
            ElecReformEIRChiller(Num).QEvaporator = 0.0;
            ElecReformEIRChiller(Num).QCondenser = 0.0;
            ElecReformEIRChiller(Num).Energy = 0.0;
            ElecReformEIRChiller(Num).EvapEnergy = 0.0;
            ElecReformEIRChiller(Num).CondEnergy = 0.0;
            ElecReformEIRChiller(Num).EvapInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).EvapInletNodeNum).Temp;
            ElecReformEIRChiller(Num).CondInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).CondInletNodeNum).Temp;
            ElecReformEIRChiller(Num).CondOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).CondOutletNodeNum).Temp;
            ElecReformEIRChiller(Num).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).EvapOutletNodeNum).Temp;
            ElecReformEIRChiller(Num).ActualCOP = 0.0;

            if (ElecReformEIRChiller(Num).HeatRecActive) {

                PlantUtilities::SafeCopyPlantNode(ElecReformEIRChiller(Num).HeatRecInletNodeNum, ElecReformEIRChiller(Num).HeatRecOutletNodeNum);
                ElecReformEIRChiller(Num).QHeatRecovery = 0.0;
                ElecReformEIRChiller(Num).EnergyHeatRecovery = 0.0;
                ElecReformEIRChiller(Num).HeatRecInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecInletNodeNum).Temp;
                ElecReformEIRChiller(Num).HeatRecOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecOutletNodeNum).Temp;
                ElecReformEIRChiller(Num).HeatRecMassFlow = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecInletNodeNum).MassFlowRate;
            }

        } else { // Chiller is running, so pass calculated values
            // Set node temperatures
            DataLoopNode::Node(ElecReformEIRChiller(Num).EvapOutletNodeNum).Temp = ElecReformEIRChiller(Num).EvapOutletTemp;
            DataLoopNode::Node(ElecReformEIRChiller(Num).CondOutletNodeNum).Temp = ElecReformEIRChiller(Num).CondOutletTemp;
            // Set node flow rates;  for these load based models
            // assume that sufficient evaporator flow rate is available
            ElecReformEIRChiller(Num).ChillerFalseLoad = ElecReformEIRChiller(Num).ChillerFalseLoadRate * DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
            ElecReformEIRChiller(Num).Energy = ElecReformEIRChiller(Num).Power * DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
            ElecReformEIRChiller(Num).EvapEnergy = ElecReformEIRChiller(Num).QEvaporator * DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
            ElecReformEIRChiller(Num).CondEnergy = ElecReformEIRChiller(Num).QCondenser * DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
            ElecReformEIRChiller(Num).EvapInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).EvapInletNodeNum).Temp;
            ElecReformEIRChiller(Num).CondInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).CondInletNodeNum).Temp;
            if (ElecReformEIRChiller(Num).Power != 0.0) {
                ElecReformEIRChiller(Num).ActualCOP = (ElecReformEIRChiller(Num).QEvaporator + ElecReformEIRChiller(Num).ChillerFalseLoadRate) / ElecReformEIRChiller(Num).Power;
            } else {
                ElecReformEIRChiller(Num).ActualCOP = 0.0;
            }

            if (ElecReformEIRChiller(Num).HeatRecActive) {

                PlantUtilities::SafeCopyPlantNode(ElecReformEIRChiller(Num).HeatRecInletNodeNum, ElecReformEIRChiller(Num).HeatRecOutletNodeNum);
                ElecReformEIRChiller(Num).EnergyHeatRecovery = ElecReformEIRChiller(Num).QHeatRecovery * DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
                DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecOutletNodeNum).Temp = ElecReformEIRChiller(Num).HeatRecOutletTemp;
                ElecReformEIRChiller(Num).HeatRecInletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecInletNodeNum).Temp;
                ElecReformEIRChiller(Num).HeatRecOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecOutletNodeNum).Temp;
                ElecReformEIRChiller(Num).HeatRecMassFlow = DataLoopNode::Node(ElecReformEIRChiller(Num).HeatRecInletNodeNum).MassFlowRate;
            }
        }
    }

    Real64 CondOutTempResidual(Real64 const FalsiCondOutTemp, // RegulaFalsi condenser outlet temperature result [C]
                               Array1<Real64> const &Par      // Parameter array used to interface with RegulaFalsi solver
    )
    {

        // FUNCTION INFORMATION:
        //       AUTHOR         Richard Raustad
        //       DATE WRITTEN   May 2006
        //       MODIFIED       L.Gu, May 2006
        //       RE-ENGINEERED

        // PURPOSE OF THIS FUNCTION:
        //  Calculates residual function (desired condenser outlet temperature)
        //  Reformulated EIR chiller requires condenser outlet temperature to calculate capacity and power.

        // METHODOLOGY EMPLOYED:
        //  Regula Falsi solver is used to calculate condenser outlet temperature.

        int EIRChillNum = int(Par(1));
        Real64 MyLoad = Par(2);
        bool RunFlag = (int(Par(3)) == 1);
        bool FirstIteration = (int(Par(4)) == 1);
        bool EquipFlowCtrl = int(Par(6));

        CalcReformEIRChillerModel(EIRChillNum, MyLoad, RunFlag, FirstIteration, EquipFlowCtrl, FalsiCondOutTemp);
        Real64 CondOutTempResidual = FalsiCondOutTemp - ElecReformEIRChiller(EIRChillNum).CondOutletTemp; // CondOutletTemp is module level variable, final value used for reporting

        return CondOutTempResidual;
    }

    void CalcReformEIRChillerModel(int const EIRChillNum,                // Chiller number
                                   Real64 &MyLoad,                       // Operating load [W]
                                   bool const RunFlag,                   // TRUE when chiller operating
                                   bool const EP_UNUSED(FirstIteration), // TRUE when first iteration of timestep !unused1208
                                   int const EquipFlowCtrl,              // Flow control mode for the equipment
                                   Real64 const FalsiCondOutTemp         // RegulaFalsi condenser outlet temperature result [C]
    )
    {

        // SUBROUTINE INFORMATION:
        //       AUTHOR         Lixing Gu, FSEC
        //       DATE WRITTEN   July 2006
        //       MODIFIED       na
        //       RE-ENGINEERED  na

        //       MODIFIED
        //			Aug. 2014, Rongpeng Zhang, added an additional part-load performance curve type
        //			Jun. 2016, Rongpeng Zhang, applied the chiller supply water temperature sensor fault model
        //			Nov. 2016, Rongpeng Zhang, added fouling chiller fault

        // PURPOSE OF THIS SUBROUTINE:
        //  Simulate a vapor compression chiller using the reformulated model developed by Mark Hydeman

        // METHODOLOGY EMPLOYED:
        //  Use empirical curve fits to model performance at off-design conditions

        // REFERENCES:
        // 1. Hydeman, M., P. Sreedharan, N. Webb, and S. Blanc. 2002. "Development and Testing of a Reformulated
        //    Regression-Based Electric Chiller Model". ASHRAE Transactions, HI-02-18-2, Vol 108, Part 2, pp. 1118-1127.

        static ObjexxFCL::gio::Fmt OutputFormat("(F6.2)");
        static std::string const RoutineName("CalcElecReformEIRChillerModel");

        ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio = 0.0;
        ElecReformEIRChiller(EIRChillNum).ChillerCyclingRatio = 0.0;
        ElecReformEIRChiller(EIRChillNum).ChillerFalseLoadRate = 0.0;
        ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = 0.0;
        ElecReformEIRChiller(EIRChillNum).CondMassFlowRate = 0.0;
        ElecReformEIRChiller(EIRChillNum).Power = 0.0;
        ElecReformEIRChiller(EIRChillNum).QCondenser = 0.0;
        ElecReformEIRChiller(EIRChillNum).QEvaporator = 0.0;
        ElecReformEIRChiller(EIRChillNum).QHeatRecovery = 0.0;
        int PlantLoopNum = ElecReformEIRChiller(EIRChillNum).CWLoopNum;
        int LoopSideNum = ElecReformEIRChiller(EIRChillNum).CWLoopSideNum;
        int BranchNum = ElecReformEIRChiller(EIRChillNum).CWBranchNum;
        int CompNum = ElecReformEIRChiller(EIRChillNum).CWCompNum;

        // Set performance curve outputs to 0.0 when chiller is off
        ElecReformEIRChiller(EIRChillNum).ChillerCapFT = 0.0;
        ElecReformEIRChiller(EIRChillNum).ChillerEIRFT = 0.0;
        ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR = 0.0;

        // Set module-level chiller evap and condenser inlet temperature variables
        Real64 CondInletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).Temp;

        // If no loop demand or chiller OFF, return
        // If chiller load is 0 or chiller is not running then leave the subroutine. Before leaving
        //  if the component control is SERIESACTIVE we set the component flow to inlet flow so that
        //  flow resolver will not shut down the branch
        if (MyLoad >= 0 || !RunFlag) {
            if (EquipFlowCtrl == DataBranchAirLoopPlant::ControlType_SeriesActive || DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock == 1) {
                ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).MassFlowRate;
            }
            if (ElecReformEIRChiller(EIRChillNum).CondenserType == WaterCooled) {
                if (DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum)
                        .LoopSide(ElecReformEIRChiller(EIRChillNum).CDLoopSideNum)
                        .Branch(ElecReformEIRChiller(EIRChillNum).CDBranchNum)
                        .Comp(ElecReformEIRChiller(EIRChillNum).CDCompNum)
                        .FlowCtrl == DataBranchAirLoopPlant::ControlType_SeriesActive) {
                    ElecReformEIRChiller(EIRChillNum).CondMassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).CondInletNodeNum).MassFlowRate;
                }
            }

            return;
        }

        // LOAD LOCAL VARIABLES FROM DATA STRUCTURE (for code readability)
        Real64 MinPartLoadRat = ElecReformEIRChiller(EIRChillNum).MinPartLoadRat;
        Real64 MaxPartLoadRat = ElecReformEIRChiller(EIRChillNum).MaxPartLoadRat;
        Real64 MinUnloadRat = ElecReformEIRChiller(EIRChillNum).MinUnloadRat;

        // Chiller reference capacity [W]
        Real64 ChillerRefCap = ElecReformEIRChiller(EIRChillNum).RefCap;

        // Reference coefficient of performance, from user input
        Real64 ReferenceCOP = ElecReformEIRChiller(EIRChillNum).RefCOP;
        ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).Temp;

        // Evaporator low temp. limit cut off [C]
        Real64 TempLowLimitEout = ElecReformEIRChiller(EIRChillNum).TempLowLimitEvapOut;

        // Maximum evaporator mass flow rate converted from volume flow rate [kg/s]
        Real64 EvapMassFlowRateMax = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRateMax;
        int PartLoadCurveType = ElecReformEIRChiller(EIRChillNum).PartLoadCurveType;

        // If there is a fault of chiller fouling
        if (ElecReformEIRChiller(EIRChillNum).FaultyChillerFoulingFlag && (!DataGlobals::WarmupFlag) && (!DataGlobals::DoingSizing) && (!DataGlobals::KickOffSimulation)) {
            int FaultIndex = ElecReformEIRChiller(EIRChillNum).FaultyChillerFoulingIndex;
            Real64 NomCap_ff = ChillerRefCap;
            Real64 ReferenceCOP_ff = ReferenceCOP;

            // calculate the Faulty Chiller Fouling Factor using fault information
            ElecReformEIRChiller(EIRChillNum).FaultyChillerFoulingFactor = FaultsManager::FaultsChillerFouling(FaultIndex).CalFoulingFactor();

            // update the Chiller nominal capacity and COP at faulty cases
            ChillerRefCap = NomCap_ff * ElecReformEIRChiller(EIRChillNum).FaultyChillerFoulingFactor;
            ReferenceCOP = ReferenceCOP_ff * ElecReformEIRChiller(EIRChillNum).FaultyChillerFoulingFactor;
        }

        // Set mass flow rates

        if (ElecReformEIRChiller(EIRChillNum).CondenserType == WaterCooled) {
            ElecReformEIRChiller(EIRChillNum).CondMassFlowRate = ElecReformEIRChiller(EIRChillNum).CondMassFlowRateMax;
            PlantUtilities::SetComponentFlowRate(ElecReformEIRChiller(EIRChillNum).CondMassFlowRate,
                                 ElecReformEIRChiller(EIRChillNum).CondInletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).CondOutletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                 ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                 ElecReformEIRChiller(EIRChillNum).CDBranchNum,
                                 ElecReformEIRChiller(EIRChillNum).CDCompNum);
            PlantUtilities::PullCompInterconnectTrigger(ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                        ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                        ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                        ElecReformEIRChiller(EIRChillNum).CWCompNum,
                                        ElecReformEIRChiller(EIRChillNum).CondMassFlowIndex,
                                        ElecReformEIRChiller(EIRChillNum).CDLoopNum,
                                        ElecReformEIRChiller(EIRChillNum).CDLoopSideNum,
                                        DataPlant::CriteriaType_MassFlowRate,
                                        ElecReformEIRChiller(EIRChillNum).CondMassFlowRate);

            if (ElecReformEIRChiller(EIRChillNum).CondMassFlowRate < DataBranchAirLoopPlant::MassFlowTolerance) return;
        }
        Real64 FRAC = 1.0;
        Real64 EvapOutletTempSetPoint(0.0); // Evaporator outlet temperature setpoint [C]
        {
            auto const SELECT_CASE_var(DataPlant::PlantLoop(PlantLoopNum).LoopDemandCalcScheme);
            if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                if ((ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) ||
                    (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).Branch(BranchNum).Comp(CompNum).CurOpSchemeType == DataPlant::CompSetPtBasedSchemeType) ||
                    (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint != DataLoopNode::SensedNodeFlagValue)) {
                    // there will be a valid setpoint on outlet
                    EvapOutletTempSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint;
                } else { // use plant loop overall setpoint
                    EvapOutletTempSetPoint = DataLoopNode::Node(DataPlant::PlantLoop(PlantLoopNum).TempSetPointNodeNum).TempSetPoint;
                }
            } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                if ((ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) ||
                    (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).Branch(BranchNum).Comp(CompNum).CurOpSchemeType == DataPlant::CompSetPtBasedSchemeType) ||
                    (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi != DataLoopNode::SensedNodeFlagValue)) {
                    // there will be a valid setpoint on outlet
                    EvapOutletTempSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi;
                } else { // use plant loop overall setpoint
                    EvapOutletTempSetPoint = DataLoopNode::Node(DataPlant::PlantLoop(PlantLoopNum).TempSetPointNodeNum).TempSetPointHi;
                }
            } else {
                assert(false);
            }
        }

        // If there is a fault of Chiller SWT Sensor (zrp_Jun2016)
        if (ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTFlag && (!DataGlobals::WarmupFlag) && (!DataGlobals::DoingSizing) && (!DataGlobals::KickOffSimulation)) {
            int FaultIndex = ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTIndex;
            Real64 EvapOutletTempSetPoint_ff = EvapOutletTempSetPoint;

            // calculate the sensor offset using fault information
            ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTOffset = FaultsManager::FaultsChillerSWTSensor(FaultIndex).CalFaultOffsetAct();
            // update the EvapOutletTempSetPoint
            EvapOutletTempSetPoint =
                max(ElecReformEIRChiller(EIRChillNum).TempLowLimitEvapOut,
                    min(DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp, EvapOutletTempSetPoint_ff - ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTOffset));
            ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTOffset = EvapOutletTempSetPoint_ff - EvapOutletTempSetPoint;
        }

        // correct temperature if using heat recovery
        // use report values for latest valid calculation, lagged somewhat
        if (ElecReformEIRChiller(EIRChillNum).HeatRecActive) {
            if ((ElecReformEIRChiller(EIRChillNum).QHeatRecovery + ElecReformEIRChiller(EIRChillNum).QCondenser) >
                0.0) { // protect div by zero
                ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp = (ElecReformEIRChiller(EIRChillNum).QHeatRecovery * ElecReformEIRChiller(EIRChillNum).HeatRecOutletTemp +
                        ElecReformEIRChiller(EIRChillNum).QCondenser * ElecReformEIRChiller(EIRChillNum).CondOutletTemp) /
                                  (ElecReformEIRChiller(EIRChillNum).QHeatRecovery + ElecReformEIRChiller(EIRChillNum).QCondenser);
            } else {
                ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp = FalsiCondOutTemp;
            }
        } else {
            ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp = FalsiCondOutTemp;
        }

        // Get capacity curve info with respect to CW setpoint and leaving condenser water temps
        ElecReformEIRChiller(EIRChillNum).ChillerCapFT = max(0.0, CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex, EvapOutletTempSetPoint, ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp));

        // Available chiller capacity as a function of temperature
        Real64 AvailChillerCap = ChillerRefCap * ElecReformEIRChiller(EIRChillNum).ChillerCapFT;

        ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).MassFlowRate;
        //   Some other component set the flow to 0. No reason to continue with calculations.
        if (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate == 0.0) {
            MyLoad = 0.0;
            return;
        }

        // This chiller is currently has only a water-cooled condenser

        // Calculate water side load
        Real64 Cp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidName,
                                   DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp,
                                   DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CWLoopNum).FluidIndex,
                                   RoutineName);

        Real64 TempLoad; // actual load to be met by chiller. This value is compared to MyLoad
        // and reset when necessary since this chiller can cycle, the load passed
        // should be the actual load.  Instead the minimum PLR * RefCap is
        // passed in.

        TempLoad = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).MassFlowRateMaxAvail * Cp * (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapOutletTempSetPoint);

        TempLoad = max(0.0, TempLoad);

        // MyLoad is capped at minimum PLR * RefCap, adjust load to actual water side load because this chiller can cycle
        if (std::abs(MyLoad) > TempLoad) {
            MyLoad = sign(TempLoad, MyLoad);
        }

        // Part load ratio based on load and available chiller capacity, cap at max part load ratio
        Real64 PartLoadRat;                 // Operating part load ratio
        if (AvailChillerCap > 0) {
            PartLoadRat = max(0.0, min(std::abs(MyLoad) / AvailChillerCap, MaxPartLoadRat));
        } else {
            PartLoadRat = 0.0;
        }

        // Set evaporator heat transfer rate
        ElecReformEIRChiller(EIRChillNum).QEvaporator = AvailChillerCap * PartLoadRat;
        ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio = PartLoadRat;
        // If FlowLock is False (0), the chiller sets the plant loop mdot
        // If FlowLock is True (1),  the new resolved plant loop mdot is used
        if (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock == 0) {
            if (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).Branch(BranchNum).Comp(CompNum).CurOpSchemeType == DataPlant::CompSetPtBasedSchemeType) {
                ElecReformEIRChiller(EIRChillNum).PossibleSubcooling = false;
            } else {
                ElecReformEIRChiller(EIRChillNum).PossibleSubcooling = true;
            }

            Real64 EvapDeltaTemp(0.0);          // Evaporator temperature difference [C]

            // Either set the flow to the Constant value or calculate the flow for the variable volume case
            if ((ElecReformEIRChiller(EIRChillNum).FlowMode == ConstantFlow) || (ElecReformEIRChiller(EIRChillNum).FlowMode == NotModulated)) {
                // Set the evaporator mass flow rate to design
                // Start by assuming max (design) flow
                ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = EvapMassFlowRateMax;
                // Use PlantUtilities::SetComponentFlowRate to decide actual flow
                PlantUtilities::SetComponentFlowRate(ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                     ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                                     ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                                     ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                     ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                     ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                     ElecReformEIRChiller(EIRChillNum).CWCompNum);
                if (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate != 0.0) {
                    EvapDeltaTemp = ElecReformEIRChiller(EIRChillNum).QEvaporator / ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate / Cp;
                } else {
                    EvapDeltaTemp = 0.0;
                }
                ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapDeltaTemp;
            } else if (ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) {
                {
                    auto const SELECT_CASE_var(DataPlant::PlantLoop(PlantLoopNum).LoopDemandCalcScheme);
                    if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                        // Calculate the Delta Temp from the inlet temp to the chiller outlet setpoint
                        EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint;
                    } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                        EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi;
                    } else {
                        assert(false);
                    }
                }

                if (EvapDeltaTemp != 0) {
                    ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = max(0.0, (ElecReformEIRChiller(EIRChillNum).QEvaporator / Cp / EvapDeltaTemp));
                    if ((ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate - EvapMassFlowRateMax) > DataBranchAirLoopPlant::MassFlowTolerance) ElecReformEIRChiller(EIRChillNum).PossibleSubcooling = true;
                    // Check to see if the Maximum is exceeded, if so set to maximum
                    ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = min(EvapMassFlowRateMax, ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate);
                    // Use PlantUtilities::SetComponentFlowRate to decide actual flow
                    PlantUtilities::SetComponentFlowRate(ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                         ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                                         ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                                         ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                         ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                         ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                         ElecReformEIRChiller(EIRChillNum).CWCompNum);
                    // Should we recalculate this with the corrected setpoint?
                    {
                        auto const SELECT_CASE_var(DataPlant::PlantLoop(PlantLoopNum).LoopDemandCalcScheme);
                        if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                            ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint;
                        } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                            ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi;
                        }
                    }
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = max(0.0, (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp));
                } else {
                    // Try to request zero flow
                    ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = 0.0;
                    // Use PlantUtilities::SetComponentFlowRate to decide actual flow
                    PlantUtilities::SetComponentFlowRate(ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                         ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                                         ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                                         ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                         ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                         ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                         ElecReformEIRChiller(EIRChillNum).CWCompNum);
                    // No deltaT since component is not running
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp;
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = 0.0;
                    PartLoadRat = 0.0;
                    ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio = PartLoadRat;

                    if (ElecReformEIRChiller(EIRChillNum).DeltaTErrCount < 1 && !DataGlobals::WarmupFlag) {
                        ++ElecReformEIRChiller(EIRChillNum).DeltaTErrCount;
                        ShowWarningError("Evaporator DeltaTemp = 0 in mass flow calculation (Tevapin = Tevapout setpoint temp).");
                        ShowContinueErrorTimeStamp("");
                    } else if (!DataGlobals::WarmupFlag) {
                        ++ElecReformEIRChiller(EIRChillNum).ChillerCapFTError;
                        ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                           "\": Evaporator DeltaTemp = 0 in mass flow calculation warning continues...",
                                                       ElecReformEIRChiller(EIRChillNum).DeltaTErrCountIndex,
                                                       EvapDeltaTemp,
                                                       EvapDeltaTemp);
                    }
                }
            } // End of Constant Variable Flow If Block

            // If there is a fault of Chiller SWT Sensor (zrp_Jun2016)
            if (ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTFlag && (!DataGlobals::WarmupFlag) && (!DataGlobals::DoingSizing) && (!DataGlobals::KickOffSimulation) &&
                (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate > 0)) {
                // calculate directly affected variables at faulty case: EvapOutletTemp, EvapMassFlowRate, QEvaporator
                int FaultIndex = ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTIndex;
                bool VarFlowFlag = (ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated);
                FaultsManager::FaultsChillerSWTSensor(FaultIndex)
                    .CalFaultChillerSWT(VarFlowFlag,
                                        ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTOffset,
                                        Cp,
                                        DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp,
                                        ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                                        ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                        ElecReformEIRChiller(EIRChillNum).QEvaporator);
                // update corresponding variables at faulty case
                PartLoadRat = (AvailChillerCap > 0.0) ? (ElecReformEIRChiller(EIRChillNum).QEvaporator / AvailChillerCap) : 0.0;
                PartLoadRat = max(0.0, min(PartLoadRat, MaxPartLoadRat));
                ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio = PartLoadRat;
            }

        } else { // If FlowLock is True
            ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).MassFlowRate;
            PlantUtilities::SetComponentFlowRate(ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                 ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum,
                                 ElecReformEIRChiller(EIRChillNum).CWLoopNum,
                                 ElecReformEIRChiller(EIRChillNum).CWLoopSideNum,
                                 ElecReformEIRChiller(EIRChillNum).CWBranchNum,
                                 ElecReformEIRChiller(EIRChillNum).CWCompNum);
            //       Some other component set the flow to 0. No reason to continue with calculations.
            if (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate == 0.0) {
                MyLoad = 0.0;
                return;
            }

            Real64 EvapDeltaTemp;

            if (ElecReformEIRChiller(EIRChillNum).PossibleSubcooling) {
                ElecReformEIRChiller(EIRChillNum).QEvaporator = std::abs(MyLoad);
                EvapDeltaTemp = ElecReformEIRChiller(EIRChillNum).QEvaporator / ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate / Cp;
                ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapDeltaTemp;
            } else {
                EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapOutletTempSetPoint;
                ElecReformEIRChiller(EIRChillNum).QEvaporator = max(0.0, (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp));
                ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = EvapOutletTempSetPoint;
            }
            if (ElecReformEIRChiller(EIRChillNum).EvapOutletTemp < TempLowLimitEout) {
                if ((DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - TempLowLimitEout) > DataPlant::DeltaTempTol) {
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = TempLowLimitEout;
                    EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp;
                } else {
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp;
                    EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp;
                }
            }
            if (ElecReformEIRChiller(EIRChillNum).EvapOutletTemp < DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempMin) {
                if ((DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempMin) > DataPlant::DeltaTempTol) {
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempMin;
                    EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp;
                } else {
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp;
                    EvapDeltaTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate * Cp * EvapDeltaTemp;
                }
            }
            // If load exceeds the distributed load set to the distributed load
            if (ElecReformEIRChiller(EIRChillNum).QEvaporator > std::abs(MyLoad)) {
                if (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate > DataBranchAirLoopPlant::MassFlowTolerance) {
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = std::abs(MyLoad);
                    EvapDeltaTemp = ElecReformEIRChiller(EIRChillNum).QEvaporator / ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate / Cp;
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapDeltaTemp;
                } else {
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = 0.0;
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp;
                }
            }

            // If there is a fault of Chiller SWT Sensor (zrp_Jun2016)
            if (ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTFlag && (!DataGlobals::WarmupFlag) && (!DataGlobals::DoingSizing) && (!DataGlobals::KickOffSimulation) &&
                (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate > 0)) {
                // calculate directly affected variables at faulty case: EvapOutletTemp, EvapMassFlowRate, QEvaporator
                int FaultIndex = ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTIndex;
                bool VarFlowFlag = false;
                FaultsManager::FaultsChillerSWTSensor(FaultIndex)
                    .CalFaultChillerSWT(VarFlowFlag,
                                        ElecReformEIRChiller(EIRChillNum).FaultyChillerSWTOffset,
                                        Cp,
                                        DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp,
                                        ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                                        ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate,
                                        ElecReformEIRChiller(EIRChillNum).QEvaporator);
                // update corresponding variables at faulty case
            }

            // Checks QEvaporator on the basis of the machine limits.
            if (ElecReformEIRChiller(EIRChillNum).QEvaporator > (AvailChillerCap * MaxPartLoadRat)) {
                if (ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate > DataBranchAirLoopPlant::MassFlowTolerance) {
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = AvailChillerCap * MaxPartLoadRat;
                    EvapDeltaTemp = ElecReformEIRChiller(EIRChillNum).QEvaporator / ElecReformEIRChiller(EIRChillNum).EvapMassFlowRate / Cp;
                    // evaporator outlet temperature is allowed to float upwards (recalculate AvailChillerCap? iterate?)
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp - EvapDeltaTemp;
                } else {
                    ElecReformEIRChiller(EIRChillNum).QEvaporator = 0.0;
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapInletNodeNum).Temp;
                }
            }

            if (AvailChillerCap > 0.0) {
                PartLoadRat = max(0.0, min((ElecReformEIRChiller(EIRChillNum).QEvaporator / AvailChillerCap), MaxPartLoadRat));
            } else {
                PartLoadRat = 0.0;
            }

            // Chiller cycles below minimum part load ratio, FRAC = amount of time chiller is ON during this time step
            if (PartLoadRat < MinPartLoadRat) FRAC = min(1.0, (PartLoadRat / MinPartLoadRat));

            // set the module level variable used for reporting FRAC
            ElecReformEIRChiller(EIRChillNum).ChillerCyclingRatio = FRAC;

            // Chiller is false loading below PLR = minimum unloading ratio, find PLR used for energy calculation
            if (AvailChillerCap > 0.0) {
                PartLoadRat = max(PartLoadRat, MinUnloadRat);
            } else {
                PartLoadRat = 0.0;
            }

            // set the module level variable used for reporting PLR
            ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio = PartLoadRat;

            // calculate the load due to false loading on chiller over and above water side load
            ElecReformEIRChiller(EIRChillNum).ChillerFalseLoadRate = (AvailChillerCap * PartLoadRat * FRAC) - ElecReformEIRChiller(EIRChillNum).QEvaporator;
            if (ElecReformEIRChiller(EIRChillNum).ChillerFalseLoadRate < DataHVACGlobals::SmallLoad) {
                ElecReformEIRChiller(EIRChillNum).ChillerFalseLoadRate = 0.0;
            }

        } // This is the end of the FlowLock Block

        ElecReformEIRChiller(EIRChillNum).ChillerEIRFT = max(0.0, CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex, ElecReformEIRChiller(EIRChillNum).EvapOutletTemp, ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp));

        // Part Load Ratio Curve Type: 1_LeavingCondenserWaterTemperature; 2_Lift  zrp_Aug2014
        if (PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
            ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR = max(0.0, CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp, PartLoadRat));
        } else if (PartLoadCurveType == PLR_Lift) {

            // Chiller lift
            Real64 ChillerLift = ElecReformEIRChiller(EIRChillNum).ChillerCondAvgTemp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;

            // Deviation of leaving chilled water temperature from the reference condition
            Real64 ChillerTdev = std::abs(ElecReformEIRChiller(EIRChillNum).EvapOutletTemp - ElecReformEIRChiller(EIRChillNum).TempRefEvapOut);

            // Chiller lift under the reference condition
            Real64 ChillerLiftRef = ElecReformEIRChiller(EIRChillNum).TempRefCondOut - ElecReformEIRChiller(EIRChillNum).TempRefEvapOut;

            if (ChillerLiftRef <= 0) ChillerLiftRef = 35 - 6.67;

            // Normalized chiller lift
            Real64 ChillerLiftNom = ChillerLift / ChillerLiftRef;

            // Normalized ChillerTdev
            Real64 ChillerTdevNom = ChillerTdev / ChillerLiftRef;

            ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR = max(0.0, CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, ChillerLiftNom, PartLoadRat, ChillerTdevNom));
        }

        if (ReferenceCOP <= 0) ReferenceCOP = 5.5;
        ElecReformEIRChiller(EIRChillNum).Power = (AvailChillerCap / ReferenceCOP) * ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR * ElecReformEIRChiller(EIRChillNum).ChillerEIRFT * FRAC;

        ElecReformEIRChiller(EIRChillNum).QCondenser = ElecReformEIRChiller(EIRChillNum).Power * ElecReformEIRChiller(EIRChillNum).CompPowerToCondenserFrac + ElecReformEIRChiller(EIRChillNum).QEvaporator + ElecReformEIRChiller(EIRChillNum).ChillerFalseLoadRate;

        //  Currently only water cooled chillers are allowed for the reformulated EIR chiller model
        if (ElecReformEIRChiller(EIRChillNum).CondMassFlowRate > DataBranchAirLoopPlant::MassFlowTolerance) {
            // If Heat Recovery specified for this vapor compression chiller, then Qcondenser will be adjusted by this subroutine
            if (ElecReformEIRChiller(EIRChillNum).HeatRecActive)
                ReformEIRChillerHeatRecovery(EIRChillNum, ElecReformEIRChiller(EIRChillNum).QCondenser, ElecReformEIRChiller(EIRChillNum).CondMassFlowRate, CondInletTemp, ElecReformEIRChiller(EIRChillNum).QHeatRecovery);
            Cp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidName,
                                       CondInletTemp,
                                       DataPlant::PlantLoop(ElecReformEIRChiller(EIRChillNum).CDLoopNum).FluidIndex,
                                       RoutineName);
            ElecReformEIRChiller(EIRChillNum).CondOutletTemp = ElecReformEIRChiller(EIRChillNum).QCondenser / ElecReformEIRChiller(EIRChillNum).CondMassFlowRate / Cp + CondInletTemp;
        } else {
            ShowSevereError("ControlReformEIRChillerModel: Condenser flow = 0, for ElecReformEIRChiller=" + ElecReformEIRChiller(EIRChillNum).Name);
            ShowContinueErrorTimeStamp("");
        }
    }

    void CheckMinMaxCurveBoundaries(int const EIRChillNum,    // Number of the current electric EIR chiller being simulated
                                    bool const FirstIteration // TRUE when first iteration of timestep
    )
    {
        // SUBROUTINE INFORMATION:
        //       AUTHOR:          R Raustad, FSEC
        //       DATE WRITTEN:    August 2006

        // PURPOSE OF THIS SUBROUTINE:
        //  To compare the evaporator/condenser outlet temperatures to curve object min/max values

        // Do not print out warnings if chiller not operating or FirstIteration/DataGlobals::WarmupFlag/FlowLock
        int PlantLoopNum = ElecReformEIRChiller(EIRChillNum).CWLoopNum;
        int LoopSideNum = ElecReformEIRChiller(EIRChillNum).CWLoopSideNum;
        int BranchNum = ElecReformEIRChiller(EIRChillNum).CWBranchNum;
        int CompNum = ElecReformEIRChiller(EIRChillNum).CWCompNum;

        if (FirstIteration || DataGlobals::WarmupFlag || DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock == 0) return;

        // Move CAPFT and EIRFT min/max values for evaporator outlet temperature to local variables

        // Minimum evaporator leaving temperature allowed by CAPFT curve [C]
        Real64 CAPFTXTmin = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMin;

        // Maximum evaporator leaving temperature allowed by CAPFT curve [C]
        Real64 CAPFTXTmax = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTXTempMax;

        // Minimum evaporator leaving temperature allowed by EIRFT curve [C]
        Real64 EIRFTXTmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMin;

        // Maximum evaporator leaving temperature allowed by EIRFT curve [C]
        Real64 EIRFTXTmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTXTempMax;

        // Check bounds for curves, lump min/max into same check since min/max values are reported in recurring warning messages
        if (ElecReformEIRChiller(EIRChillNum).EvapOutletTemp < CAPFTXTmin || ElecReformEIRChiller(EIRChillNum).EvapOutletTemp > CAPFTXTmax) {
            ++ElecReformEIRChiller(EIRChillNum).CAPFTXIter;
            if (ElecReformEIRChiller(EIRChillNum).CAPFTXIter == 1) {
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                 "\": The evaporator outlet temperature (" + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).EvapOutletTemp, 2) +
                                 " C) is outside the range of evaporator outlet temperatures (X var) given in Cooling Capacity Function of "
                                 "Temperature biquadratic curve = " +
                                 ElecReformEIRChiller(EIRChillNum).CAPFTName);
                ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(CAPFTXTmin, 2) + " C to " + General::TrimSigDigits(CAPFTXTmax, 2) + " C.");
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": The evap outlet temp range in Cooling Capacity Function of Temp curve error continues.",
                                               ElecReformEIRChiller(EIRChillNum).CAPFTXIterIndex,
                                               ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                                               ElecReformEIRChiller(EIRChillNum).EvapOutletTemp);
            } else {
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": The evap outlet temp range in Cooling Capacity Function of Temp curve error continues.",
                                               ElecReformEIRChiller(EIRChillNum).CAPFTXIterIndex,
                                               ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                                               ElecReformEIRChiller(EIRChillNum).EvapOutletTemp);
            }
        }

        if (ElecReformEIRChiller(EIRChillNum).EvapOutletTemp < EIRFTXTmin || ElecReformEIRChiller(EIRChillNum).EvapOutletTemp > EIRFTXTmax) {
            ++ElecReformEIRChiller(EIRChillNum).EIRFTXIter;
            if (ElecReformEIRChiller(EIRChillNum).EIRFTXIter == 1) {
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                 "\": The evaporator outlet temperature (" + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).EvapOutletTemp, 2) +
                                 " C) is outside the range of evaporator outlet temperatures (X var) given in Electric Input to Cooling Output Ratio "
                                 "Function of Temperature biquadratic curve = " +
                                 ElecReformEIRChiller(EIRChillNum).EIRFTName);
                ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(EIRFTXTmin, 2) + " C to " + General::TrimSigDigits(EIRFTXTmax, 2) + " C.");
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The evap outlet temp range in Electric Input to Cooling Output Ratio Function of Temp curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFTXIterIndex,
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp);
            } else {
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The evap outlet temp range in Electric Input to Cooling Output Ratio Function of Temp curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFTXIterIndex,
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp,
                    ElecReformEIRChiller(EIRChillNum).EvapOutletTemp);
            }
        }

        // Move CAPFT, EIRFT, and EIRFPLR min/max condenser outlet temperature values to local variables

        // Minimum condenser  leaving temperature allowed by CAPFT curve [C]
        Real64 CAPFTYTmin = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMin;

        // Maximum condenser  leaving temperature allowed by CAPFT curve [C]
        Real64 CAPFTYTmax = ElecReformEIRChiller(EIRChillNum).ChillerCAPFTYTempMax;

        // Minimum condenser  leaving temperature allowed by EIRFT curve [C]
        Real64 EIRFTYTmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMin;

        // Maximum condenser  leaving temperature allowed by EIRFT curve [C]
        Real64 EIRFTYTmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFTYTempMax;

        Real64 EIRFPLRTmin(0.0);                 // Minimum condenser  leaving temperature allowed by EIRFPLR curve [C]
        Real64 EIRFPLRTmax(0.0);                 // Maximum condenser  leaving temperature allowed by EIRFPLR curve [C]

        if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
            EIRFPLRTmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMin;
            EIRFPLRTmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRTempMax;
        }

        // Move EIRFPLR min/max part-load ratio values to local variables

        // Minimum PLR allowed by EIRFPLR curve
        Real64 EIRFPLRPLRmin = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMin;

        // Maximum PLR allowed by EIRFPLR curve
        Real64 EIRFPLRPLRmax = ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRPLRMax;

        // Check bounds for curves, lump min/max into same check since min/max values are reported in recurring warning messages
        if (ElecReformEIRChiller(EIRChillNum).CondOutletTemp < CAPFTYTmin || ElecReformEIRChiller(EIRChillNum).CondOutletTemp > CAPFTYTmax) {
            ++ElecReformEIRChiller(EIRChillNum).CAPFTYIter;
            if (ElecReformEIRChiller(EIRChillNum).CAPFTYIter == 1) {
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                 "\": The condenser outlet temperature (" + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 2) +
                                 " C) is outside the range of condenser outlet temperatures (Y var) given in Cooling Capacity Function of "
                                 "Temperature biquadratic curve = " +
                                 ElecReformEIRChiller(EIRChillNum).CAPFTName);
                ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(CAPFTYTmin, 2) + " C to " + General::TrimSigDigits(CAPFTYTmax, 2) + " C.");
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": The cond outlet temp range in Cooling Capacity Function of Temp curve error continues.",
                                               ElecReformEIRChiller(EIRChillNum).CAPFTYIterIndex,
                                               ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                                               ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
            } else {
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": The cond outlet temp range in Cooling Capacity Function of Temp curve error continues.",
                                               ElecReformEIRChiller(EIRChillNum).CAPFTYIterIndex,
                                               ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                                               ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
            }
        }

        if (ElecReformEIRChiller(EIRChillNum).CondOutletTemp < EIRFTYTmin || ElecReformEIRChiller(EIRChillNum).CondOutletTemp > EIRFTYTmax) {
            ++ElecReformEIRChiller(EIRChillNum).EIRFTYIter;
            if (ElecReformEIRChiller(EIRChillNum).EIRFTYIter == 1) {
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                 "\": The condenser outlet temperature (" + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 2) +
                                 " C) is outside the range of condenser outlet temperatures (Y var) given in Electric Input to Cooling Output Ratio "
                                 "Function of Temperature biquadratic curve = " +
                                 ElecReformEIRChiller(EIRChillNum).EIRFTName);
                ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(EIRFTYTmin, 2) + " C to " + General::TrimSigDigits(EIRFTYTmax, 2) + " C.");
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The cond outlet temp range in Electric Input to Cooling Output Ratio as a Function of Temp curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFTYIterIndex,
                    ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                    ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
            } else {
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The cond outlet temp range in Electric Input to Cooling Output Ratio as a Function of Temp curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFTYIterIndex,
                    ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                    ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
            }
        }

        if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
            if (ElecReformEIRChiller(EIRChillNum).CondOutletTemp < EIRFPLRTmin || ElecReformEIRChiller(EIRChillNum).CondOutletTemp > EIRFPLRTmax) {
                ++ElecReformEIRChiller(EIRChillNum).EIRFPLRTIter;
                if (ElecReformEIRChiller(EIRChillNum).EIRFPLRTIter == 1) {
                    ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                     "\": The condenser outlet temperature (" + General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 2) +
                                     " C) is outside the range of condenser outlet temperatures (X var) given in Electric Input to Cooling Output "
                                     "Ratio Function of Part-load Ratio bicubic curve = " +
                                     ElecReformEIRChiller(EIRChillNum).EIRFPLRName);
                    ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(EIRFPLRTmin, 2) + " C to " + General::TrimSigDigits(EIRFPLRTmax, 2) +
                                               " C.");
                    ShowRecurringWarningErrorAtEnd(
                        "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                            "\": The cond outlet temp range in Electric Input to Cooling Output Ratio Function of PLR curve error continues.",
                        ElecReformEIRChiller(EIRChillNum).EIRFPLRTIterIndex,
                        ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                        ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
                } else {
                    ShowRecurringWarningErrorAtEnd(
                        "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                            "\": The cond outlet temp range in Electric Input to Cooling Output Ratio Function of PLR curve error continues.",
                        ElecReformEIRChiller(EIRChillNum).EIRFPLRTIterIndex,
                        ElecReformEIRChiller(EIRChillNum).CondOutletTemp,
                        ElecReformEIRChiller(EIRChillNum).CondOutletTemp);
                }
            }
        }

        if (ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio < EIRFPLRPLRmin || ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio > EIRFPLRPLRmax) {
            ++ElecReformEIRChiller(EIRChillNum).EIRFPLRPLRIter;
            if (ElecReformEIRChiller(EIRChillNum).EIRFPLRPLRIter == 1) {
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name + "\": The part-load ratio (" +
                                 General::TrimSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio, 3) +
                                 ") is outside the range of part-load ratios (Y var) given in Electric Input to Cooling Output Ratio Function of "
                                 "Part-load Ratio bicubic curve = " +
                                 ElecReformEIRChiller(EIRChillNum).EIRFPLRName);
                ShowContinueErrorTimeStamp("The range specified = " + General::TrimSigDigits(EIRFPLRPLRmin, 3) + " to " + General::TrimSigDigits(EIRFPLRPLRmax, 3) +
                                           '.');
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The part-load ratio range in Electric Input to Cooling Output Ratio Function of PLRatio curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFPLRPLRIterIndex,
                    ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio,
                    ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio);
            } else {
                ShowRecurringWarningErrorAtEnd(
                    "CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                        "\": The part-load ratio range in Electric Input to Cooling Output Ratio Function of PLRatio curve error continues.",
                    ElecReformEIRChiller(EIRChillNum).EIRFPLRPLRIterIndex,
                    ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio,
                    ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio);
            }
        }

        Real64 EvapOutletTempSetPoint(0.0); // Evaporator outlet temperature setpoint [C]

        {
            auto const SELECT_CASE_var(DataPlant::PlantLoop(PlantLoopNum).LoopDemandCalcScheme);
            if (SELECT_CASE_var == DataPlant::SingleSetPoint) {
                if ((ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) ||
                    (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).Branch(BranchNum).Comp(CompNum).CurOpSchemeType == DataPlant::CompSetPtBasedSchemeType) ||
                    (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint != DataLoopNode::SensedNodeFlagValue)) {
                    // there will be a valid setpoint on outlet
                    EvapOutletTempSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPoint;
                } else { // use plant loop overall setpoint
                    EvapOutletTempSetPoint = DataLoopNode::Node(DataPlant::PlantLoop(PlantLoopNum).TempSetPointNodeNum).TempSetPoint;
                }
            } else if (SELECT_CASE_var == DataPlant::DualSetPointDeadBand) {
                if ((ElecReformEIRChiller(EIRChillNum).FlowMode == LeavingSetPointModulated) ||
                    (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).Branch(BranchNum).Comp(CompNum).CurOpSchemeType == DataPlant::CompSetPtBasedSchemeType) ||
                    (DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi != DataLoopNode::SensedNodeFlagValue)) {
                    // there will be a valid setpoint on outlet
                    EvapOutletTempSetPoint = DataLoopNode::Node(ElecReformEIRChiller(EIRChillNum).EvapOutletNodeNum).TempSetPointHi;
                } else { // use plant loop overall setpoint
                    EvapOutletTempSetPoint = DataLoopNode::Node(DataPlant::PlantLoop(PlantLoopNum).TempSetPointNodeNum).TempSetPointHi;
                }
            } else {
                assert(false);
            }
        }

        ElecReformEIRChiller(EIRChillNum).ChillerCapFT = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerCapFTIndex, EvapOutletTempSetPoint, ElecReformEIRChiller(EIRChillNum).CondOutletTemp);

        if (ElecReformEIRChiller(EIRChillNum).ChillerCapFT < 0) {
            if (ElecReformEIRChiller(EIRChillNum).ChillerCapFTError < 1 && DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 &&
                !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerCapFTError;
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name + "\":");
                ShowContinueError(" Chiller Capacity as a Function of Temperature curve output is negative (" + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerCapFT, 3) +
                                  ").");
                ShowContinueError(" Negative value occurs using an Evaporator Leaving Temp of " + General::RoundSigDigits(EvapOutletTempSetPoint, 1) +
                                  " and a Condenser Leaving Temp of " + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 1) + '.');
                ShowContinueErrorTimeStamp(" Resetting curve output to zero and continuing simulation.");
            } else if (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 && !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerCapFTError;
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": Chiller Capacity as a Function of Temperature curve output is negative warning continues...",
                                               ElecReformEIRChiller(EIRChillNum).ChillerCapFTErrorIndex,
                                               ElecReformEIRChiller(EIRChillNum).ChillerCapFT,
                                               ElecReformEIRChiller(EIRChillNum).ChillerCapFT);
            }
        }

        ElecReformEIRChiller(EIRChillNum).ChillerEIRFT = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFTIndex, ElecReformEIRChiller(EIRChillNum).EvapOutletTemp, ElecReformEIRChiller(EIRChillNum).CondOutletTemp);

        if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFT < 0.0) {
            if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFTError < 1 && DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 &&
                !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerEIRFTError;
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name + "\":");
                ShowContinueError(" Reformulated Chiller EIR as a Function of Temperature curve output is negative (" +
                                  General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerEIRFT, 3) + ").");
                ShowContinueError(" Negative value occurs using an Evaporator Leaving Temp of " + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).EvapOutletTemp, 1) +
                                  " and a Condenser Leaving Temp of " + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 1) + '.');
                ShowContinueErrorTimeStamp(" Resetting curve output to zero and continuing simulation.");
            } else if (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 && !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerEIRFTError;
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": Chiller EIR as a Function of Temperature curve output is negative warning continues...",
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFTErrorIndex,
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFT,
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFT);
            }
        }

        if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_LeavingCondenserWaterTemperature) {
            ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, ElecReformEIRChiller(EIRChillNum).CondOutletTemp, ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio);
        } else if (ElecReformEIRChiller(EIRChillNum).PartLoadCurveType == PLR_Lift) {

            // Chiller lift  [C]
            Real64 ChillerLift = ElecReformEIRChiller(EIRChillNum).CondOutletTemp - ElecReformEIRChiller(EIRChillNum).EvapOutletTemp;

            // Deviation of leaving chilled water temperature from the reference condition
            Real64 ChillerTdev = std::abs(ElecReformEIRChiller(EIRChillNum).EvapOutletTemp - ElecReformEIRChiller(EIRChillNum).TempRefEvapOut);

            // Chiller lift under the reference condition  [C]
            Real64 ChillerLiftRef = ElecReformEIRChiller(EIRChillNum).TempRefCondOut - ElecReformEIRChiller(EIRChillNum).TempRefEvapOut;

            if (ChillerLiftRef <= 0) ChillerLiftRef = 35 - 6.67;

            // Normalized chiller lift
            Real64 ChillerLiftNom = ChillerLift / ChillerLiftRef;

            // Normalized ChillerTdev
            Real64 ChillerTdevNom = ChillerTdev / ChillerLiftRef;

            ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR = CurveManager::CurveValue(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRIndex, ChillerLiftNom, ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio, ChillerTdevNom);
        }

        if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR < 0.0) {
            if (ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRError < 1 && DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 &&
                !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRError;
                ShowWarningError("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name + "\":");
                ShowContinueError(" Chiller EIR as a function of PLR and condenser water temperature curve output is negative (" +
                                  General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR, 3) + ").");
                ShowContinueError(" Negative value occurs using a part-load ratio of " + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).ChillerPartLoadRatio, 3) +
                                  " and a Condenser Leaving Temp of " + General::RoundSigDigits(ElecReformEIRChiller(EIRChillNum).CondOutletTemp, 1) + " C.");
                ShowContinueErrorTimeStamp(" Resetting curve output to zero and continuing simulation.");
            } else if (DataPlant::PlantLoop(PlantLoopNum).LoopSide(LoopSideNum).FlowLock != 0 && !DataGlobals::WarmupFlag) {
                ++ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRError;
                ShowRecurringWarningErrorAtEnd("CHILLER:ELECTRIC:REFORMULATEDEIR \"" + ElecReformEIRChiller(EIRChillNum).Name +
                                                   "\": Chiller EIR as a function of PLR curve output is negative warning continues...",
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLRErrorIndex,
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR,
                                               ElecReformEIRChiller(EIRChillNum).ChillerEIRFPLR);
            }
        }
    }

} // namespace ChillerReformulatedEIR

} // namespace EnergyPlus
