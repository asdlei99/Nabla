// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#ifndef __NBL_ASSET_I_CPU_DESCRIPTOR_SET_H_INCLUDED__
#define __NBL_ASSET_I_CPU_DESCRIPTOR_SET_H_INCLUDED__

#include "irr/asset/IAsset.h"
#include "irr/asset/ICPUBufferView.h"
#include "irr/asset/ICPUImageView.h"
#include "irr/asset/ICPUSampler.h"
#include "irr/asset/ICPUDescriptorSetLayout.h"
#include "irr/asset/IDescriptorSet.h"

namespace irr
{
namespace asset
{


class ICPUDescriptorSet final : public IDescriptorSet<ICPUDescriptorSetLayout>, public IAsset, public impl::IEmulatedDescriptorSet<ICPUDescriptorSetLayout>
{
		using impl_t = impl::IEmulatedDescriptorSet<ICPUDescriptorSetLayout>;
	public:
		using base_t = IDescriptorSet<ICPUDescriptorSetLayout>;

		//! Contructor preallocating memory for SDescriptorBindings which user can fill later (using non-const getDescriptors()).
		//! @see getDescriptors()
		ICPUDescriptorSet(core::smart_refctd_ptr<ICPUDescriptorSetLayout>&& _layout) : base_t(std::move(_layout)), IAsset(), impl_t(m_layout.get())
		{
		}


		inline size_t conservativeSizeEstimate() const override
		{
			return sizeof(void*)+m_descriptors->size()*sizeof(SDescriptorInfo)+m_bindingInfo->size()*sizeof(impl::IEmulatedDescriptorSet<ICPUDescriptorSetLayout>::SBindingInfo);
		}

        core::smart_refctd_ptr<IAsset> clone(uint32_t _depth = ~0u) const override
        {
            auto layout = (_depth > 0u && m_layout) ? core::smart_refctd_ptr_static_cast<ICPUDescriptorSetLayout>(m_layout->clone(_depth - 1u)) : m_layout;
            auto cp = core::make_smart_refctd_ptr<ICPUDescriptorSet>(std::move(layout));
            clone_common(cp.get());

            const uint32_t max_ix = getMaxDescriptorBindingIndex();
            for (uint32_t i = 0u; i <= max_ix; ++i)
            {
                auto cloneDescriptor = [](const core::smart_refctd_ptr<IDescriptor>& _desc, uint32_t _depth) -> core::smart_refctd_ptr<IDescriptor> {
                    if (!_desc)
                        return nullptr;

                    IAsset* asset = nullptr;
                    switch (_desc->getTypeCategory())
                    {
                    case IDescriptor::EC_BUFFER:
                        asset = static_cast<ICPUBuffer*>(_desc.get()); break;
                    case IDescriptor::EC_BUFFER_VIEW:
                        asset = static_cast<ICPUBufferView*>(_desc.get()); break;
                    case IDescriptor::EC_IMAGE:
                        asset = static_cast<ICPUImageView*>(_desc.get()); break;
                    }

                    auto cp = asset->clone(_depth);

                    switch (_desc->getTypeCategory())
                    {
                    case IDescriptor::EC_BUFFER:
                        return core::smart_refctd_ptr_static_cast<ICPUBuffer>(std::move(cp));
                    case IDescriptor::EC_BUFFER_VIEW:
                        return core::smart_refctd_ptr_static_cast<ICPUBufferView>(std::move(cp));
                    case IDescriptor::EC_IMAGE:
                        return core::smart_refctd_ptr_static_cast<ICPUImageView>(std::move(cp));
                    }
                    return nullptr;
                };

                auto desc = getDescriptors(i);
                auto cp_desc = cp->getDescriptors(i);

                const E_DESCRIPTOR_TYPE type = getDescriptorsType(i);
                for (uint32_t d = 0u; d < desc.size(); ++d)
                {
                    cp_desc.begin()[d].assign(desc.begin()[d], type);
                    if (_depth > 0u)
                    {
                        cp_desc.begin()[d].desc = cloneDescriptor(cp_desc.begin()[d].desc, _depth-1u);
                        if (cp_desc.begin()[d].image.sampler && type==EDT_COMBINED_IMAGE_SAMPLER)
                            cp_desc.begin()[d].image.sampler = core::smart_refctd_ptr_static_cast<ICPUSampler>(cp_desc.begin()[d].image.sampler->clone(_depth-1u));
                    }
                }
            }

            return cp;
        }

		inline void convertToDummyObject(uint32_t referenceLevelsBelowToConvert=0u) override
		{
            if (isDummyObjectForCacheAliasing)
                return;
            convertToDummyObject_common(referenceLevelsBelowToConvert);

			if (referenceLevelsBelowToConvert)
			{
                --referenceLevelsBelowToConvert;
				m_layout->convertToDummyObject(referenceLevelsBelowToConvert);
				for (auto it=m_descriptors->begin(); it!=m_descriptors->end(); it++)
				{
					auto descriptor = it->desc.get();
					if (!descriptor)
						continue;
					switch (descriptor->getTypeCategory())
					{
						case IDescriptor::EC_BUFFER:
							static_cast<asset::ICPUBuffer*>(descriptor)->convertToDummyObject(referenceLevelsBelowToConvert);
							break;
						case IDescriptor::EC_IMAGE:
							static_cast<asset::ICPUImageView*>(descriptor)->convertToDummyObject(referenceLevelsBelowToConvert);
							if (descriptor->getTypeCategory()==IDescriptor::EC_IMAGE && it->image.sampler)
								it->image.sampler->convertToDummyObject(referenceLevelsBelowToConvert);
							break;
						case IDescriptor::EC_BUFFER_VIEW:
							static_cast<asset::ICPUBufferView*>(descriptor)->convertToDummyObject(referenceLevelsBelowToConvert);
							break;
					}
				}
			}
            //dont drop descriptors so that we can access GPU descriptors through driver->getGPUObjectsFromAssets()
			//m_descriptors = nullptr;
			//m_bindingInfo = nullptr;
		}

		_NBL_STATIC_INLINE_CONSTEXPR auto AssetType = ET_DESCRIPTOR_SET;
		inline E_TYPE getAssetType() const override { return AssetType; }

		inline ICPUDescriptorSetLayout* getLayout() { return m_layout.get(); }
		inline const ICPUDescriptorSetLayout* getLayout() const { return m_layout.get(); }

		//!
		inline uint32_t getMaxDescriptorBindingIndex() const
		{
			return m_bindingInfo ? static_cast<uint32_t>(m_bindingInfo->size()-1u):0u;
		}

		//!
		inline E_DESCRIPTOR_TYPE getDescriptorsType(uint32_t index) const
		{
			if (m_bindingInfo && index<m_bindingInfo->size())
				return m_bindingInfo->operator[](index).descriptorType;
			return EDT_INVALID;
		}

		//! Can modify the array of descriptors bound to a particular bindings
		inline core::SRange<SDescriptorInfo> getDescriptors(uint32_t index) 
		{ 
			if (m_bindingInfo && index<m_bindingInfo->size())
			{
				const auto& info = m_bindingInfo->operator[](index);
				auto _begin = m_descriptors->begin()+info.offset;
				if (index+1u!=m_bindingInfo->size())
					return core::SRange<SDescriptorInfo>{_begin, m_descriptors->begin()+m_bindingInfo->operator[](index+1u).offset};
				else
					return core::SRange<SDescriptorInfo>{_begin, m_descriptors->end()};
			}
			else
				return core::SRange<SDescriptorInfo>{nullptr, nullptr};
		}
		inline core::SRange<const SDescriptorInfo> getDescriptors(uint32_t index) const
		{
			if (m_bindingInfo && index<m_bindingInfo->size())
			{
				const auto& info = m_bindingInfo->operator[](index);
				auto _begin = m_descriptors->begin()+info.offset;
				if (index+1u!=m_bindingInfo->size())
					return core::SRange<const SDescriptorInfo>{_begin, m_descriptors->begin()+m_bindingInfo->operator[](index+1u).offset};
				else
					return core::SRange<const SDescriptorInfo>{_begin, m_descriptors->end()};
			}
			else
				return core::SRange<const SDescriptorInfo>{nullptr, nullptr};
		}

		inline auto getTotalDescriptorCount() const
		{
			return m_descriptors->size();
		}

	protected:
		virtual ~ICPUDescriptorSet() = default;
};

}
}

#endif