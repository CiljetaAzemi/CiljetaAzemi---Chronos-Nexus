#pragma once

enum class ResourceType {
    Food,
    Water,
    Energy,
    Fuel,
    Medicine,
    RareMinerals,
    ConstructionMaterials
};

class Resource {
public:
    ResourceType type;

    double quantity;
    double productionRate;
    double consumptionRate;
    double storageCapacity;
    double importance;
};
