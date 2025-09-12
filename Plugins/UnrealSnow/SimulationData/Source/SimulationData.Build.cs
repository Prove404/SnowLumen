namespace UnrealBuildTool.Rules
{
	public class SimulationData : ModuleRules
	{
		public SimulationData(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
					"SimulationData/Private"
                }
                );

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
                      "Core", "CoreUObject", "Engine", "RenderCore", "Landscape", "RHI",
                        "SimplexNoise", "ShaderUtility", "WorldClimData"
                }
				);
		}
	}
}