// Product identity shim. Core code must never hardcode the product name so
// core/ stays byte-identical across products (plain copy to sync); only
// app/product.h differs per product.

#pragma once

#define PRODUCT_NAME "SesameMaker"
