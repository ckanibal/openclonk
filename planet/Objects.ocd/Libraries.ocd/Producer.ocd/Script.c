/**
	Producer
	Library for production facilities. This library handles the automatic production of 
	items in structures. The library provides the interface for the player, checks for 
	components, need for liquids or fuel and power. Then handles the production process and may in the future
	provide an interface with other systems (e.g. railway).
	
	@author Maikel
*/

/*
	Properties of producers:
	* Storage of producers:
	  * Producers can store sufficient amounts of raw material they need to produce their products.
	  * Producers can store the products they can produce.
	  * Nothing more, nothing less.
	* Production queue:
	  * Producers automatically produce the items in the production queue.
	  * Producible items can be added to the queue, with an amount specified.
	  * Also an infinite amount is possible, equals indefinite production.
	Possible interaction with cable network:
	* Producers request the cable network for raw materials.
	
*/

#include Library_PowerConsumer

// Production queue, a list of items to be produced.
// Contains proplists of format {Product = <objid>, Amount = <int>, Infinite = (optional)<bool>}. /Infinite/ == true -> infinite production.
local queue;


protected func Initialize()
{
	queue = [];
	AddEffect("ProcessQueue", this, 100, 5, this);
	return _inherited(...);
}

/*-- Player interface --*/

// All producers are accessible. 
public func IsContainer() { return true; }
// Provides an own interaction menu, even if it wouldn't be a container.
public func HasInteractionMenu() { return true; }

public func GetConsumerPriority() { return 50; }

public func GetProductionMenuEntries()
{
	var products = GetProducts();
	
	// default design of a control menu item
	var control_prototype =
	{
		BackgroundColor = {Std = 0, Selected = RGBa(200, 0, 0, 200)},
		OnMouseIn = GuiAction_SetTag("Selected"),
		OnMouseOut = GuiAction_SetTag("Std")
	};

	var custom_entry = 
	{
		Right = "8em%", Bottom = "4em",
		// BackgroundColor = {Std = 0, OnHover = 0x50ff0000},
		image = {Prototype = control_prototype, Right = "4em", Style = GUI_TextBottom | GUI_TextRight},
		controls = 
		{
			Left = "4em",
			remove = {Prototype = control_prototype, Bottom = "2em", Symbol = Icon_Exit, Tooltip = "$QueueRemove$"},
			endless = {Prototype = control_prototype, Top = "2em", Symbol = Icon_Play, Tooltip = "$QueueInf$"}
		}
	};
	
	var menu_entries = [];
	var index = 0;
	for (var product in products)
	{
		++index;
		// Check if entry is already in queue.
		var info;
		for (info in queue)
		{
			if (info.Product == product) break;
			info = nil;
		}
		// Prepare menu entry.
		var entry = new custom_entry 
		{
			image = new custom_entry.image{}, 
			controls = new custom_entry.controls
			{
				remove = new custom_entry.controls.remove{},
				endless = new custom_entry.controls.endless{},
			}
		};
		
		entry.image.Symbol = product;
		if (info) // Currently in queue?
		{
			if (info.Infinite)
				entry.image.Text = "$infinite$";
			else // normal amount
				entry.image.Text = Format("%dx", info.Amount);
			entry.controls.remove.OnClick = GuiAction_Call(this, "ModifyProduction", {Product = product, Amount = -1});
			entry.BackgroundColor = RGB(50, 50, 50);
		}
		else
			entry.controls.remove = nil;
			
		entry.Priority = 1000 * product->GetValue() + index; // Sort by (estimated) value and then by index.
		entry.Tooltip = product->GetName();
		entry.image.OnClick = GuiAction_Call(this, "ModifyProduction", {Product = product, Amount = +1});
		entry.controls.endless.OnClick = GuiAction_Call(this, "ModifyProduction", {Product = product, Infinite = true}); 
		PushBack(menu_entries, {symbol = product, extra_data = nil, custom = entry});
	}
	return menu_entries;
}


public func GetInteractionMenus(object clonk)
{
	var menus = _inherited() ?? [];
	var prod_menu =
	{
		title = "$Production$",
		entries_callback = this.GetProductionMenuEntries,
		callback = nil, // The callback is handled internally. See GetProductionMenuEntries.
		callback_hover = "OnProductHover",
		callback_target = this,
		BackgroundColor = RGB(0, 0, 50),
		Priority = 20
	};
	PushBack(menus, prod_menu);
	
	return menus;
}

public func OnProductHover(symbol, extra_data, desc_menu_target, menu_id)
{
	var new_box =
	{
		Text = Format("%s:|%s", symbol.Name, symbol.Description,),
		requirements = 
		{
			Top = "100% - 2em",
			Style = GUI_TextBottom
		}
	};
	
	var product_id = symbol;
	var costs = ProductionCosts(product_id);
	var cost_msg = "";
	var liquid, material;
	for (var comp in costs)
		cost_msg = Format("%s %s {{%i}}", cost_msg, GetCostString(comp[1], CheckComponent(comp[0], comp[1])), comp[0]);
	if (this->~FuelNeed(product_id))
		cost_msg = Format("%s %s {{Icon_Producer_Fuel}}", cost_msg, GetCostString(1, CheckFuel(product_id)));
	if (liquid = this->~LiquidNeed(product_id))
		cost_msg = Format("%s %s {{Icon_Producer_%s}}", cost_msg, GetCostString(liquid[1], CheckLiquids(product_id)), liquid[0]);
	if (material = this->~MaterialNeed(product_id))
		cost_msg = Format("%s %s {{%i}}", cost_msg, GetCostString(material[1], CheckMaterials(product_id)), product_id->~GetMaterialIcon(material[0]));
	if (this->~PowerNeed(product_id))
		cost_msg = Format("%s + {{Library_PowerConsumer}}", cost_msg);
	new_box.requirements.Text = cost_msg;
	GuiUpdate(new_box, menu_id, 1, desc_menu_target);
}

private func GetCostString(int amount, bool available)
{
	// Format amount to colored string; make it red if it's not available
	if (available) return Format("%dx", amount);
	return Format("<c ff0000>%dx</c>", amount);
}

/*-- Production  properties --*/

// This function may be overloaded by the actual producer.
// If set to true, the producer will show every product which is assigned to it instead of checking the knowledge base of its owner.
private func IgnoreKnowledge() { return false; }

/** Determines whether the product specified can be produced. Should be overloaded by the producer.
	@param product_id item's id of which to determine if it is producible.
	@return \c true if the item can be produced, \c false otherwise.
*/
private func IsProduct(id product_id)
{
	return false;
}

/** Returns an array with the ids of products which can be produced at this producer.
	@return array with products.
*/
public func GetProducts(object for_clonk)
{
	var for_plr = GetOwner();
	if (for_clonk)
		for_plr = for_clonk-> GetOwner();
	var products = [];
	// Cycle through all definitions to find the ones this producer can produce.
	var index = 0, product;
	if (!IgnoreKnowledge() && for_plr != NO_OWNER)
	{
		while (product = GetPlrKnowledge(for_plr, nil, index, C4D_Object))
		{
			if (IsProduct(product))
				products[GetLength(products)] = product;
			index++;
		}
		index = 0;
		while (product = GetPlrKnowledge(for_plr, nil, index, C4D_Vehicle))
		{
			if (IsProduct(product))
				products[GetLength(products)] = product;
			index++;
		}
	} 
	else
	{
		while (product = GetDefinition(index))
		{
			if (IsProduct(product))
				products[GetLength(products)] = product;
			index++;	
		}
	}
	return products;
}

/** Determines whether the raw material specified is needed for production. Should be overloaded by the producer.
	@param rawmat_id id of raw material for which to determine if it is needed for production.
	@return \c true if the raw material is needed, \c false otherwise.
*/
public func NeedRawMaterial(id rawmat_id)
{
	return false; // Obsolete for now.
}

/**
	Determines the production costs for an item.
	@param item_id id of the item under consideration.
	@return a list of objects and their respective amounts.
*/
public func ProductionCosts(id item_id)
{
	/* NOTE: This may be overloaded by the producer */
	var comp_list = [];
	var comp_id, index = 0;
	while (comp_id = GetComponent(nil, index, nil, item_id))
	{
		var amount = GetComponent(comp_id, index, nil, item_id);
		comp_list[index] = [comp_id, amount];
		index++;		
	}
	return comp_list;
}

/*-- Production queue --*/

/** Returns the queue index corresponding to a product id or nil.
*/
public func GetQueueIndex(id product_id)
{
	var i = 0, l = GetLength(queue);
	for (;i < l; ++i)
	{
		if (queue[i].Product == product_id) return i;
	}
	return nil;
}

/** Modifies an item in the queue. The index can be retrieved via GetQueueIndex.
	@param position index in the queue
	@param amount change of amount or nil
	@param infinite_production Sets the state of infinite production for the item. Can also be nil to not modify anything.
	@return False if the item was in the queue and has now been removed. True otherwise. 
*/
public func ModifyQueueIndex(int position, int amount, bool infinite_production)
{
	// safety
	var queue_length = GetLength(queue);
	if (position >= queue_length) return true;
	
	var item = queue[position];
	
	if (infinite_production != nil)
		item.Infinite = infinite_production;
	item.Amount += amount;
	
	// It might be necessary to remove the item from the queue.
	if (!item.Infinite && item.Amount <= 0)
	{
		// Move all things on the right one slot to the left.
		var index = position;
		while (++index < queue_length)
			queue[index - 1] = queue[index];
		SetLength(queue, queue_length - 1);
		return false;
	}
	return true;
}
/** Adds an item to the production queue.
	@param product_id id of the item.
	@param amount the amount of items of \c item_id which should be produced. Amount must not be negative.
	@paramt infinite whether to enable infinite production.
*/
public func AddToQueue(id product_id, int amount, bool infinite)
{
	// Check if this producer can produce the requested item.
	if (!IsProduct(product_id))
		return nil;
	if (amount < 0) FatalError("Producer::AddToQueue called with negative amount.");
	
	// if the product is already in the queue, just modify the amount
	var found = false;
	for (var info in queue)
	{
		if (info.Product != product_id) continue;
		info.Amount += amount;
		if (infinite != nil) info.Infinite = infinite;
		found = true;
		break;
	}

	// Otherwise create a new entry in the queue.
	if (!found)
		PushBack(queue, { Product = product_id, Amount = amount, Infinite = infinite});
	// Notify all production menus open for this producer.
	UpdateInteractionMenus(this.GetProductionMenuEntries);
}


/** Shifts the queue one space to the left. The first item will be put in the very right slot.
*/
public func CycleQueue()
{
	if (GetLength(queue) <= 1) return;
	var first = queue[0];
	var queue_length = GetLength(queue);
	for (var i = 1; i < queue_length; ++i)
		queue[i - 1] = queue[i];
	queue[-1] = first;
}

/** Clears the complete production queue.
*/
public func ClearQueue(bool abort)
{
	queue = [];
	UpdateInteractionMenus(this.GetProductionMenuEntries);
	return;
}

/** Modifies a certain production item arbitrarily. This is mainly used by the interaction menu.
	This also creates a new production order if none exists yet.
	@param info
		proplist with Product, Amount, Infinite. If Infinite is set to true, it acts as a toggle. 
*/
public func ModifyProduction(proplist info)
{
	var index = GetQueueIndex(info.Product);
	if (index == nil && (info.Amount > 0 || info.Infinite))
	{
		AddToQueue(info.Product, info.Amount, info.Infinite);
	}
	else
	{
		// Toggle infinity?
		if (queue[index].Infinite)
		{
			if (info.Infinite || info.Amount < 0)
				info.Infinite = false;
		}
		ModifyQueueIndex(index, info.Amount, info.Infinite);
	}
	UpdateInteractionMenus(this.GetProductionMenuEntries);
}

/** Returns the current queue.
	@return an array containing the queue elements (.Product for id, .Amount for amount).
*/
public func GetQueue()
{
	return queue;
}

protected func FxProcessQueueStart()
{

	return 1;
}

protected func FxProcessQueueTimer(object target, proplist effect)
{
	// If target is currently producing, don't do anything.
	if (IsProducing())
		return 1;

	// Wait if there are no items in the queue.
	if (!queue[0])
		return 1;
	
	// Produce first item in the queue.
	var product_id = queue[0].Product;
	// Check raw material need.
	if (!CheckMaterial(product_id))
	{
		// No material available? request from cable network.
		RequestMaterial(product_id);
		// In the meanwhile, just cycle the queue and try the next one.
		CycleQueue();
		return 1;
	}
	// Start the item production.
	if (!Produce(product_id))
		return 1;
		
	// Update queue, reduce amount.
	var is_still_there = ModifyQueueIndex(0, -1);
	// And cycle to enable rotational production of (infinite) objects.
	if (is_still_there)
		CycleQueue();
	// We changed something. Update menus.
	UpdateInteractionMenus(this.GetProductionMenuEntries);
	// Done with production checks.
	return 1;
}

/*-- Production --*/

// These functions may be overloaded by the actual producer.
private func ProductionTime(id product) { return product->~GetProductionTime(); }
private func FuelNeed(id product) { return product->~GetFuelNeed(); }
private func LiquidNeed(id product) { return product->~GetLiquidNeed(); }
private func MaterialNeed(id product) { return product->~GetMaterialNeed(); }

private func PowerNeed() { return 200; }

private func Produce(id product)
{
	// Already producing? Wait a little.
	if (IsProducing())
		return false;	
		
	// Check if components are available.
	if (!CheckComponents(product))
		return false;
	// Check need for fuel.
	if (!CheckFuel(product))
		return false;
	// Check need for liquids.
	if (!CheckLiquids(product))
		return false;
	// Check need for materials.
	if (!CheckMaterials(product))
		return false;
	// Check need for power.
	if (!CheckForPower())
		return false;

	// Everything available? Start production.
	// Remove needed components, fuel and liquid.
	// Power will be substracted during the production process.
	CheckComponents(product, true);
	CheckFuel(product, true);
	CheckLiquids(product, true);
	CheckMaterials(product, true);
	
	// Add production effect.
	AddEffect("ProcessProduction", this, 100, 2, this, nil, product);

	return true;
}

private func CheckComponents(id product, bool remove)
{
	for (var item in ProductionCosts(product))
	{
		var mat_id = item[0];
		var mat_cost = item[1];
		if (!CheckComponent(mat_id, mat_cost))
			return false; // Components missing.
		else if (remove)
		{
			for (var i = 0; i < mat_cost; i++)
				 FindObject(Find_Container(this), Find_ID(mat_id))->RemoveObject();
		}
	}
	return true;
}

public func CheckComponent(id component, int amount)
{
	// check if at least the given amount of the given component is available to be used for production
	return (ObjectCount(Find_Container(this), Find_ID(component)) >= amount);
}

public func CheckFuel(id product, bool remove)
{
	if (FuelNeed(product) > 0)
	{
		var fuel_amount = 0;
		// Find fuel in this producer.
		for (var fuel in FindObjects(Find_Container(this), Find_Func("IsFuel")))
			fuel_amount += fuel->~GetFuelAmount();
		if (fuel_amount < FuelNeed(product))
			return false;
		else if (remove)
		{
			// Remove the fuel needed.
			fuel_amount = 0;
			for (var fuel in FindObjects(Find_Container(this), Find_Func("IsFuel")))
			{
				fuel_amount += fuel->~GetFuelAmount();
				fuel->RemoveObject();
				if (fuel_amount >= FuelNeed(product))
					break;
			}			
		}
	}
	return true;
}

public func CheckLiquids(id product, bool remove)
{
	var liq_need = LiquidNeed(product);
	if (liq_need)
	{
		var liquid_amount = 0;
		var liquid = liq_need[0];
		var need = liq_need[1];
		// Find liquid containers in this producer.
		for (var liq_container in FindObjects(Find_Container(this), Find_Func("IsLiquidContainer")))
			if (liq_container->~GetBarrelMaterial() == liquid)
				liquid_amount += liq_container->~GetFillLevel();
		// Find objects that "are" liquid (e.g. ice)
		for (var liq_object in FindObjects(Find_Container(this), Find_Func("IsLiquid")))
			if (liq_object->~IsLiquid() == liquid)
				liquid_amount += liq_object->~GetLiquidAmount();
		if (liquid_amount < need)
			return false;
		else if (remove)
		{
			// Remove the liquid needed.
			var extracted = 0;
			for (var liq_container in FindObjects(Find_Container(this), Find_Func("IsLiquidContainer")))
			{
				var val = liq_container->~GetLiquid(liquid, need - extracted);
				extracted += val[1];
				if (extracted >= need)
					return true;
			}
			for (var liq_object in FindObjects(Find_Container(this), Find_Func("IsLiquid")))
			{
				if (liq_object->~IsLiquid() != liquid) continue;
				extracted += liq_object->~GetLiquidAmount();
				liq_object->RemoveObject();
				if (extracted >= need)
					break;
			}
		}		
	}
	return true;
}

public func CheckMaterials(id product, bool remove)
{
	var mat_need = MaterialNeed(product);
	if (mat_need)
	{
		var material_amount = 0;
		var material = mat_need[0];
		var need = mat_need[1];
		// Find liquid containers in this producer.
		for (var mat_container in FindObjects(Find_Container(this), Find_Func("IsMaterialContainer")))
			if (mat_container->~GetContainedMaterial() == material)
				material_amount += mat_container->~GetFillLevel();
		if (material_amount < need)
			return false;
		else if (remove)
		{
			// Remove the material needed.
			var extracted = 0;
			for (var mat_container in FindObjects(Find_Container(this), Find_Func("IsMaterialContainer")))
			{
				var val = mat_container->~RemoveContainedMaterial(material, need - extracted);
				extracted += val;
				if (extracted >= need)
					break;
			}
		}
	}
	return true;
}

private func CheckForPower()
{
	return true; // always assume that power is available
}

private func IsProducing()
{
	if (GetEffect("ProcessProduction", this))
		return true;
	return false;
}

protected func FxProcessProductionStart(object target, proplist effect, int temporary, id product)
{
	if (temporary)
		return 1;
		
	// Set product.
	effect.Product = product;
		
	// Set production duration to zero.
	effect.Duration = 0;
	
	// Production is active.
	effect.Active = true;

	// Callback to the producer.
	this->~OnProductionStart(effect.Product);
	
	// consume power
	if(PowerNeed() > 0)
		MakePowerConsumer(PowerNeed());
	
	return 1;
}

public func OnNotEnoughPower()
{
	var effect = GetEffect("ProcessProduction", this);
	if(effect)
	{
		effect.Active = false;
		this->~OnProductionHold(effect.Product, effect.Duration);
	} 
	else
		FatalError("Producer effect removed when power still active!");
	return _inherited(...);
}

public func OnEnoughPower()
{
	var effect = GetEffect("ProcessProduction", this);
	if(effect)
	{
		effect.Active = true;
		this->~OnProductionContinued(effect.Product, effect.Duration);
	} 
	else 
		FatalError("Producer effect removed when power still active!");
	return _inherited(...);
}

protected func FxProcessProductionTimer(object target, proplist effect, int time)
{
	if (!effect.Active)
		return 1;
	
	// Add effect interval to production duration.
	effect.Duration += effect.Interval;
	
	//Log("Production in progress on %i, %d frames, %d time", effect.Product, effect.Duration, time);
	
	// Check if production time has been reached.
	if (effect.Duration >= ProductionTime(effect.Product))
		return -1;
	
	return 1;
}

protected func FxProcessProductionStop(object target, proplist effect, int reason, bool temp)
{
	if(temp) return;
	
	// no need to consume power anymore
	UnmakePowerConsumer();
		
	if (reason != 0)
		return 1;
		
	// Callback to the producer.
	//Log("Production finished on %i after %d frames", effect.Product, effect.Duration);
	this->~OnProductionFinish(effect.Product);
	// Create product. 	
	var product = CreateObject(effect.Product);
	OnProductEjection(product);
	
	return 1;
}

// Standard behaviour for product ejection.
public func OnProductEjection(object product)
{
	// Safety for the product removing itself on construction.
	if (!product)
		return;	
	// Vehicles in front fo buildings.
	if (product->GetCategory() & C4D_Vehicle)
	{
		var x = GetX();
		var y = GetY() + GetDefHeight()/2 - product->GetDefHeight()/2;
		product->SetPosition(x, y);
		// Sometimes, there is material in front of the building. Move vehicles upwards in that case
		var max_unstick_range = Max(GetDefHeight()/5,5); // 8 pixels for tools workshop
		var y_off = 0;
		while (product->Stuck() && y_off < max_unstick_range)
			product->SetPosition(x, y-++y_off);
	}
	// Items should stay inside.
	else
		product->Enter(this);
	return;
}

/*-- --*/

/**
	Determines whether there is sufficient material to produce an item.
*/
private func CheckMaterial(id item_id)
{
	for (var item in ProductionCosts(item_id))
	{
		var mat_id = item[0];
		var mat_cost = item[1];
		var mat_av = ObjectCount(Find_Container(this), Find_ID(mat_id));
		if (mat_av < mat_cost)
			return false;
	}
	return true;
}

/**
	Requests the necessary material from the cable network if available.
*/
private func RequestMaterial(id item_id)
{
	for (var item in ProductionCosts(item_id))
	{
		var mat_id = item[0];
		var mat_cost = item[1];
		var mat_av = ObjectCount(Find_Container(this), Find_ID(mat_id));
		if (mat_av < mat_cost)
			RequestObject(mat_id, mat_cost - mat_av);
	}
	return true;
}

// Must exist if Library_CableStation is not included by either this
// library or the structure including this library.
public func RequestObject(id obj_id, int amount)
{
	return _inherited(obj_id, amount, ...);
}

/*-- Storage --*/

protected func RejectCollect(id item, object obj)
{
	// Just return RejectEntrance for this object.
	return RejectEntrance(obj);
}

protected func RejectEntrance(object obj)
{
	var obj_id = obj->GetID();
	// Products itself may be collected.
	if (IsProduct(obj_id))
		return false;
		
	// Components of products may be collected.
	for (var product in GetProducts())
	{
		var i = 0, comp_id;
		while (comp_id = GetComponent(nil, i, nil, product))
		{
			if (comp_id == obj_id)
				return false;
			i++;
		}
	}
	// Fuel for products may be collected.
	if (obj->~IsFuel())
	{
		for (var product in GetProducts())
			if (FuelNeed(product) > 0)
				return false;
	}
	// Liquid objects may be collected if a product needs them.
	if (obj->~IsLiquid())
	{
		for (var product in GetProducts())
			if (LiquidNeed(product))
				if (LiquidNeed(product)[0] == obj->~IsLiquid())
					return false;
	}
	// Liquid containers may be collected if a product needs them.
	if (obj->~IsLiquidContainer())
	{
		for (var product in GetProducts())
			if (LiquidNeed(product))
				return false;
	}
	// Material containers may be collected if a product needs them.
	if (obj->~IsMaterialContainer())
	{
		for (var product in GetProducts())
			if (MaterialNeed(product))
				return false;
	}
	return true;
}
