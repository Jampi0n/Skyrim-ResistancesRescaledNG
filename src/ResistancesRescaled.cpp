#include "ResistancesRescaled.h"


namespace ResistancesRescaled {

	constexpr size_t ELEMENTS_PER_AV = 3;
	constexpr size_t PARAMETERS_PER_AV = 10;

	constexpr auto MAGIC_RESIST = RE::ActorValue::kResistMagic;
	constexpr auto FIRE_RESIST = RE::ActorValue::kResistFire;
	constexpr auto FROST_RESIST = RE::ActorValue::kResistFrost;
	constexpr auto SHOCK_RESIST = RE::ActorValue::kResistShock;
	constexpr auto DAMAGE_RESIST = RE::ActorValue::kDamageResist;
	constexpr auto POISON_RESIST = RE::ActorValue::kPoisonResist;

	constexpr size_t ID_MAGIC = 0;
	constexpr size_t ID_ELEMENTAL = 1;
	constexpr size_t ID_FIRE = 2;
	constexpr size_t ID_FROST = 3;
	constexpr size_t ID_SHOCK = 4;
	constexpr size_t ID_ARMOR = 5;
	constexpr size_t ID_POISON = 6;


	/// <summary>
	/// Modifies a specific actor value of an actor by a certain value. Works like the papyrus function with the same name.
	/// </summary>
	/// <param name="akActor">The actor, whose actor value is modifed.</param>
	/// <param name="avID">The actor value id of the actor value that is modified.</param>
	/// <param name="mod">The value by how much the actor value is modified.</param>
	void ModActorValue(RE::Actor* akActor, RE::ActorValue av, float mod) {
		akActor->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, av, mod);
	}

	float GetActorValue(RE::Actor* akActor, RE::ActorValue av) {
		return akActor->AsActorValueOwner()->GetActorValue(av);
	}

#define GET_DATA_VANILLA_VALUE(index) data[index * ELEMENTS_PER_AV]
#define GET_DATA_MAPPED_VALUE(index) data[index * ELEMENTS_PER_AV+1]

#define FORCE_UPDATE data[21]
#define UPDATE_RUNNING data[22]
#define RESET_RESISTANCE data[23]
#define UPDATE_MASK data[24]
#define RESISTANCE_ENABLED_MASK data[25]
#define MOD_ENABLED_VALUE data[26]
#define AV_RESCALED data[27]
#define AV_RESET data[28]
#define AV_UPDATED data[29]

#define RESET(av, id) ModActorValue(akActor, av, static_cast<float>(GET_DATA_MAPPED_VALUE(id) - GET_DATA_VANILLA_VALUE(id))); \
	GET_DATA_MAPPED_VALUE(id) = 0; \
	GET_DATA_VANILLA_VALUE(id) = 0; \
	AV_RESET |= (1u << id); \
	AV_UPDATED |= (1u << id)

	/// <summary>
	/// Rescales actor value x (vanilla value) using the given parameters and returns the rescaled value.
	/// </summary>
	/// <param name="x">Vanilla actor value before rescaling.</param>
	/// <param name="parameters">Array of parameters. Semantics depends on the formula (always first element).</param>
	/// <param name="parameterOffset">Array offset for 'functionParameters'.</param>
	/// <returns>The new rescaled vallue.</returns>
	int32_t Internal_RescaleFunction(int32_t x, std::vector<float>& parameters, size_t parameterOffset) {
		double result = x;

		// Parameters:
		// parameters[0] = formula (int)
		// parameters[1] = at0
		// parameters[2] = atHigh
		// parameters[3] = highValue
		// parameters[4] = scalingFactor

		long formula = std::lround(parameters[0 + parameterOffset]);
		long at0 = std::lround(parameters[1 + parameterOffset]);
		long atHigh = std::lround(parameters[2 + parameterOffset]);
		long highValue = std::lround(parameters[3 + parameterOffset]);
		double scalingFactor = parameters[4 + parameterOffset];

		if (x < 0.) {
			result = at0 / scalingFactor + x;
		} else {
			if (formula == 0) {
				double max = 100. / scalingFactor;
				double a = 1. / (1. - 0.01 * at0) * scalingFactor;
				double b = 1. / (1. - 0.01 * atHigh) * scalingFactor;
				double c = (b - a) / highValue;
				result = max - 100. / (c * x + a);
			} else {
				double max = 100.0 / scalingFactor;
				double factor = (1. - 0.01 * at0) / scalingFactor;
				double base = std::pow((100. - atHigh) / (100. - at0), 1.0 / highValue);
				result = max - 100. * std::pow(base, x) * factor;
			}
		}
		return static_cast<int32_t>(std::lround(result));
	}

	std::vector<int32_t> RescaleSingle(RE::Actor* akActor, RE::ActorValue actorValue, std::vector<int32_t>& data, int32_t id, std::vector<float>& functionParameters, int32_t parameterId, bool forceUpdate, bool doRescaling, std::vector<RE::SpellItem*> displaySpells)
	{
		// The actual resistance value in the last update.
		int32_t lastValue = data[id * ELEMENTS_PER_AV];
		// The actual resistance value in the current update.
		int32_t newValue = (int32_t)GetActorValue(akActor, actorValue);

		// The difference in actual resistance values represents by how much the actor's
		// resistance value changed since the last update
		int32_t difference = newValue - lastValue;

		// If resistance values did not change, only update if forceUpdate
		if (difference != 0 || forceUpdate) {
			AV_UPDATED |= (1u << id);
			// The vanilla resistance value in the last update.
			int32_t combinedValue = data[id * ELEMENTS_PER_AV + 1];

			// Update to vanilla resistance value  in current update.
			combinedValue += difference;

			// Save in array.
			data[id * ELEMENTS_PER_AV + 1] = combinedValue;

			if (doRescaling) {
				// Calculate new rescaled result.
				// This only depends on the current vanilla resistance value.
				int32_t newRescaled = Internal_RescaleFunction(combinedValue, functionParameters, parameterId * PARAMETERS_PER_AV);

				// This is the difference between desired (rescaled) and actual resistance.
				// Using ModActorValue("...",  modValue) will bring the resistance to the desired value.
				int32_t modValue = newRescaled - newValue;
				ModActorValue(akActor, actorValue, static_cast<float>(modValue));

				// Save modValue in array. This will be lastValue in the next update.
				data[id * ELEMENTS_PER_AV] = newValue + modValue;
				AV_RESCALED |= (1u << id);
			} else {
				data[id * ELEMENTS_PER_AV] = combinedValue;
			}
			size_t spellIndex = id;
			RE::SpellItem* spell;
			auto mag = data[id * ELEMENTS_PER_AV];
			displaySpells[spellIndex * 2]->effects[0]->effectItem.magnitude = mag;
			displaySpells[spellIndex * 2 + 1]->effects[0]->effectItem.magnitude = mag;
		}
		return data;
	}

	std::vector<int32_t> RescaleAll(RE::Actor* akActor, std::vector<int32_t>& data, int32_t mask, std::vector<float>& functionParameters, bool forceUpdate, std::vector<RE::SpellItem*> displaySpells) {

		RescaleSingle(akActor, MAGIC_RESIST, data, ID_MAGIC, functionParameters, ID_MAGIC, forceUpdate, mask & 0x1, displaySpells);

		RescaleSingle(akActor, FIRE_RESIST, data, ID_FIRE, functionParameters, ID_ELEMENTAL, forceUpdate, mask & 0x2, displaySpells);
		RescaleSingle(akActor, FROST_RESIST, data, ID_FROST, functionParameters, ID_ELEMENTAL, forceUpdate, mask & 0x2, displaySpells);
		RescaleSingle(akActor, SHOCK_RESIST, data, ID_SHOCK, functionParameters, ID_ELEMENTAL, forceUpdate, mask & 0x2, displaySpells);

		RescaleSingle(akActor, DAMAGE_RESIST, data, ID_ARMOR, functionParameters, ID_ARMOR, forceUpdate, mask & 0x4, displaySpells);

		RescaleSingle(akActor, POISON_RESIST, data, ID_POISON, functionParameters, ID_POISON, forceUpdate, mask & 0x8, displaySpells);

		return data;
	}

	void ApplyReset(RE::Actor* akActor, std::vector<int32_t>& data) {
		int32_t resetFlags = data[23];
		int32_t prevFlags = resetFlags;
		if (resetFlags >= 8) {
			RESET(POISON_RESIST, ID_POISON);
			resetFlags -= 8;
		}
		if (resetFlags >= 4) {
			RESET(DAMAGE_RESIST, ID_ARMOR);
			resetFlags -= 4;
		}
		if (resetFlags >= 2) {
			RESET(FIRE_RESIST, ID_FIRE);
			RESET(FROST_RESIST, ID_FROST);
			RESET(SHOCK_RESIST, ID_SHOCK);
			resetFlags -= 2;
		}
		if (resetFlags >= 1) {
			RESET(MAGIC_RESIST, ID_MAGIC);
			resetFlags -= 1;
		}
		data[23] -= prevFlags;

	}

	std::vector<int32_t> MainLoop(RE::StaticFunctionTag*, RE::Actor* akActor, std::vector<int32_t> data, std::vector<float> floatParameters, std::vector<RE::SpellItem*> displaySpells) {
		AV_RESCALED = 0;
		AV_RESET = 0;
		AV_UPDATED = 0;

		if (!MOD_ENABLED_VALUE) {
			UPDATE_RUNNING = 0;
			RESET_RESISTANCE = 0xf;
			ApplyReset(akActor, data);
		} else {
			UPDATE_MASK = RESISTANCE_ENABLED_MASK;

			RescaleAll(akActor, data, UPDATE_MASK, floatParameters, static_cast<bool>(FORCE_UPDATE), displaySpells);
			FORCE_UPDATE = 0;

			if (RESET_RESISTANCE > 0) {
				ApplyReset(akActor, data);
			}

		}
		AV_RESCALED = static_cast<int32_t>(static_cast<uint32_t>(AV_RESCALED) & ~static_cast<uint32_t>(AV_RESET));
		return data;
	}

	/// <summary>
	/// Papyrus wrapper for the internal rescale function.
	/// </summary>
	/// <param name="base">PapyrusVM.</param>
	/// <param name="x">Vanilla actor value before rescaling.</param>
	/// <param name="functionParameters">Array of parameters. Semantics depends on the formula (always first element).</param>
	/// <param name="parameterOffset">Array offset for 'functionParameters'.</param>
	/// <returns>The new rescaled vallue.</returns>
	int32_t RescaleFunction(RE::StaticFunctionTag* base, int32_t x, std::vector<float> functionParameters, int32_t parameterOffset) {
		return Internal_RescaleFunction(x, functionParameters, parameterOffset);
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm) {
		vm->RegisterFunction("JRR_MainLoop", "JRR_NativeFunctions", ResistancesRescaled::MainLoop);
		vm->RegisterFunction("JRR_RescaleFunction", "JRR_NativeFunctions", ResistancesRescaled::RescaleFunction);
		return true;
	}
}