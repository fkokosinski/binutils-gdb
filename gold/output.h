// output.h -- manage the output file for gold   -*- C++ -*-

// Copyright 2006, 2007, 2008 Free Software Foundation, Inc.
// Written by Ian Lance Taylor <iant@google.com>.

// This file is part of gold.

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
// MA 02110-1301, USA.

#ifndef GOLD_OUTPUT_H
#define GOLD_OUTPUT_H

#include <list>
#include <vector>

#include "elfcpp.h"
#include "layout.h"
#include "reloc-types.h"

namespace gold
{

class General_options;
class Object;
class Symbol;
class Output_file;
class Output_section;
class Relocatable_relocs;
class Target;
template<int size, bool big_endian>
class Sized_target;
template<int size, bool big_endian>
class Sized_relobj;

// An abtract class for data which has to go into the output file.

class Output_data
{
 public:
  explicit Output_data()
    : address_(0), data_size_(0), offset_(-1),
      is_address_valid_(false), is_data_size_valid_(false),
      is_offset_valid_(false),
      dynamic_reloc_count_(0)
  { }

  virtual
  ~Output_data();

  // Return the address.  For allocated sections, this is only valid
  // after Layout::finalize is finished.
  uint64_t
  address() const
  {
    gold_assert(this->is_address_valid_);
    return this->address_;
  }

  // Return the size of the data.  For allocated sections, this must
  // be valid after Layout::finalize calls set_address, but need not
  // be valid before then.
  off_t
  data_size() const
  {
    gold_assert(this->is_data_size_valid_);
    return this->data_size_;
  }

  // Return the file offset.  This is only valid after
  // Layout::finalize is finished.  For some non-allocated sections,
  // it may not be valid until near the end of the link.
  off_t
  offset() const
  {
    gold_assert(this->is_offset_valid_);
    return this->offset_;
  }

  // Reset the address and file offset.  This essentially disables the
  // sanity testing about duplicate and unknown settings.
  void
  reset_address_and_file_offset()
  {
    this->is_address_valid_ = false;
    this->is_offset_valid_ = false;
    this->is_data_size_valid_ = false;
    this->do_reset_address_and_file_offset();
  }

  // Return the required alignment.
  uint64_t
  addralign() const
  { return this->do_addralign(); }

  // Return whether this has a load address.
  bool
  has_load_address() const
  { return this->do_has_load_address(); }

  // Return the load address.
  uint64_t
  load_address() const
  { return this->do_load_address(); }

  // Return whether this is an Output_section.
  bool
  is_section() const
  { return this->do_is_section(); }

  // Return whether this is an Output_section of the specified type.
  bool
  is_section_type(elfcpp::Elf_Word stt) const
  { return this->do_is_section_type(stt); }

  // Return whether this is an Output_section with the specified flag
  // set.
  bool
  is_section_flag_set(elfcpp::Elf_Xword shf) const
  { return this->do_is_section_flag_set(shf); }

  // Return the output section that this goes in, if there is one.
  Output_section*
  output_section()
  { return this->do_output_section(); }

  // Return the output section index, if there is an output section.
  unsigned int
  out_shndx() const
  { return this->do_out_shndx(); }

  // Set the output section index, if this is an output section.
  void
  set_out_shndx(unsigned int shndx)
  { this->do_set_out_shndx(shndx); }

  // Set the address and file offset of this data, and finalize the
  // size of the data.  This is called during Layout::finalize for
  // allocated sections.
  void
  set_address_and_file_offset(uint64_t addr, off_t off)
  {
    this->set_address(addr);
    this->set_file_offset(off);
    this->finalize_data_size();
  }

  // Set the address.
  void
  set_address(uint64_t addr)
  {
    gold_assert(!this->is_address_valid_);
    this->address_ = addr;
    this->is_address_valid_ = true;
  }

  // Set the file offset.
  void
  set_file_offset(off_t off)
  {
    gold_assert(!this->is_offset_valid_);
    this->offset_ = off;
    this->is_offset_valid_ = true;
  }

  // Finalize the data size.
  void
  finalize_data_size()
  {
    if (!this->is_data_size_valid_)
      {
	// Tell the child class to set the data size.
	this->set_final_data_size();
	gold_assert(this->is_data_size_valid_);
      }
  }

  // Set the TLS offset.  Called only for SHT_TLS sections.
  void
  set_tls_offset(uint64_t tls_base)
  { this->do_set_tls_offset(tls_base); }

  // Return the TLS offset, relative to the base of the TLS segment.
  // Valid only for SHT_TLS sections.
  uint64_t
  tls_offset() const
  { return this->do_tls_offset(); }

  // Write the data to the output file.  This is called after
  // Layout::finalize is complete.
  void
  write(Output_file* file)
  { this->do_write(file); }

  // This is called by Layout::finalize to note that the sizes of
  // allocated sections must now be fixed.
  static void
  layout_complete()
  { Output_data::allocated_sizes_are_fixed = true; }

  // Used to check that layout has been done.
  static bool
  is_layout_complete()
  { return Output_data::allocated_sizes_are_fixed; }

  // Count the number of dynamic relocations applied to this section.
  void
  add_dynamic_reloc()
  { ++this->dynamic_reloc_count_; }

  // Return the number of dynamic relocations applied to this section.
  unsigned int
  dynamic_reloc_count() const
  { return this->dynamic_reloc_count_; }

  // Whether the address is valid.
  bool
  is_address_valid() const
  { return this->is_address_valid_; }

  // Whether the file offset is valid.
  bool
  is_offset_valid() const
  { return this->is_offset_valid_; }

  // Whether the data size is valid.
  bool
  is_data_size_valid() const
  { return this->is_data_size_valid_; }

 protected:
  // Functions that child classes may or in some cases must implement.

  // Write the data to the output file.
  virtual void
  do_write(Output_file*) = 0;

  // Return the required alignment.
  virtual uint64_t
  do_addralign() const = 0;

  // Return whether this has a load address.
  virtual bool
  do_has_load_address() const
  { return false; }

  // Return the load address.
  virtual uint64_t
  do_load_address() const
  { gold_unreachable(); }

  // Return whether this is an Output_section.
  virtual bool
  do_is_section() const
  { return false; }

  // Return whether this is an Output_section of the specified type.
  // This only needs to be implement by Output_section.
  virtual bool
  do_is_section_type(elfcpp::Elf_Word) const
  { return false; }

  // Return whether this is an Output_section with the specific flag
  // set.  This only needs to be implemented by Output_section.
  virtual bool
  do_is_section_flag_set(elfcpp::Elf_Xword) const
  { return false; }

  // Return the output section, if there is one.
  virtual Output_section*
  do_output_section()
  { return NULL; }

  // Return the output section index, if there is an output section.
  virtual unsigned int
  do_out_shndx() const
  { gold_unreachable(); }

  // Set the output section index, if this is an output section.
  virtual void
  do_set_out_shndx(unsigned int)
  { gold_unreachable(); }

  // This is a hook for derived classes to set the data size.  This is
  // called by finalize_data_size, normally called during
  // Layout::finalize, when the section address is set.
  virtual void
  set_final_data_size()
  { gold_unreachable(); }

  // A hook for resetting the address and file offset.
  virtual void
  do_reset_address_and_file_offset()
  { }

  // Set the TLS offset.  Called only for SHT_TLS sections.
  virtual void
  do_set_tls_offset(uint64_t)
  { gold_unreachable(); }

  // Return the TLS offset, relative to the base of the TLS segment.
  // Valid only for SHT_TLS sections.
  virtual uint64_t
  do_tls_offset() const
  { gold_unreachable(); }

  // Functions that child classes may call.

  // Set the size of the data.
  void
  set_data_size(off_t data_size)
  {
    gold_assert(!this->is_data_size_valid_);
    this->data_size_ = data_size;
    this->is_data_size_valid_ = true;
  }

  // Get the current data size--this is for the convenience of
  // sections which build up their size over time.
  off_t
  current_data_size_for_child() const
  { return this->data_size_; }

  // Set the current data size--this is for the convenience of
  // sections which build up their size over time.
  void
  set_current_data_size_for_child(off_t data_size)
  {
    gold_assert(!this->is_data_size_valid_);
    this->data_size_ = data_size;
  }

  // Return default alignment for the target size.
  static uint64_t
  default_alignment();

  // Return default alignment for a specified size--32 or 64.
  static uint64_t
  default_alignment_for_size(int size);

 private:
  Output_data(const Output_data&);
  Output_data& operator=(const Output_data&);

  // This is used for verification, to make sure that we don't try to
  // change any sizes of allocated sections after we set the section
  // addresses.
  static bool allocated_sizes_are_fixed;

  // Memory address in output file.
  uint64_t address_;
  // Size of data in output file.
  off_t data_size_;
  // File offset of contents in output file.
  off_t offset_;
  // Whether address_ is valid.
  bool is_address_valid_;
  // Whether data_size_ is valid.
  bool is_data_size_valid_;
  // Whether offset_ is valid.
  bool is_offset_valid_;
  // Count of dynamic relocations applied to this section.
  unsigned int dynamic_reloc_count_;
};

// Output the section headers.

class Output_section_headers : public Output_data
{
 public:
  Output_section_headers(const Layout*,
			 const Layout::Segment_list*,
			 const Layout::Section_list*,
			 const Layout::Section_list*,
			 const Stringpool*);

 protected:
  // Write the data to the file.
  void
  do_write(Output_file*);

  // Return the required alignment.
  uint64_t
  do_addralign() const
  { return Output_data::default_alignment(); }

 private:
  // Write the data to the file with the right size and endianness.
  template<int size, bool big_endian>
  void
  do_sized_write(Output_file*);

  const Layout* layout_;
  const Layout::Segment_list* segment_list_;
  const Layout::Section_list* section_list_;
  const Layout::Section_list* unattached_section_list_;
  const Stringpool* secnamepool_;
};

// Output the segment headers.

class Output_segment_headers : public Output_data
{
 public:
  Output_segment_headers(const Layout::Segment_list& segment_list);

 protected:
  // Write the data to the file.
  void
  do_write(Output_file*);

  // Return the required alignment.
  uint64_t
  do_addralign() const
  { return Output_data::default_alignment(); }

 private:
  // Write the data to the file with the right size and endianness.
  template<int size, bool big_endian>
  void
  do_sized_write(Output_file*);

  const Layout::Segment_list& segment_list_;
};

// Output the ELF file header.

class Output_file_header : public Output_data
{
 public:
  Output_file_header(const Target*,
		     const Symbol_table*,
		     const Output_segment_headers*,
		     const char* entry);

  // Add information about the section headers.  We lay out the ELF
  // file header before we create the section headers.
  void set_section_info(const Output_section_headers*,
			const Output_section* shstrtab);

 protected:
  // Write the data to the file.
  void
  do_write(Output_file*);

  // Return the required alignment.
  uint64_t
  do_addralign() const
  { return Output_data::default_alignment(); }

 private:
  // Write the data to the file with the right size and endianness.
  template<int size, bool big_endian>
  void
  do_sized_write(Output_file*);

  // Return the value to use for the entry address.
  template<int size>
  typename elfcpp::Elf_types<size>::Elf_Addr
  entry();

  const Target* target_;
  const Symbol_table* symtab_;
  const Output_segment_headers* segment_header_;
  const Output_section_headers* section_header_;
  const Output_section* shstrtab_;
  const char* entry_;
};

// Output sections are mainly comprised of input sections.  However,
// there are cases where we have data to write out which is not in an
// input section.  Output_section_data is used in such cases.  This is
// an abstract base class.

class Output_section_data : public Output_data
{
 public:
  Output_section_data(off_t data_size, uint64_t addralign)
    : Output_data(), output_section_(NULL), addralign_(addralign)
  { this->set_data_size(data_size); }

  Output_section_data(uint64_t addralign)
    : Output_data(), output_section_(NULL), addralign_(addralign)
  { }

  // Return the output section.
  const Output_section*
  output_section() const
  { return this->output_section_; }

  // Record the output section.
  void
  set_output_section(Output_section* os);

  // Add an input section, for SHF_MERGE sections.  This returns true
  // if the section was handled.
  bool
  add_input_section(Relobj* object, unsigned int shndx)
  { return this->do_add_input_section(object, shndx); }

  // Given an input OBJECT, an input section index SHNDX within that
  // object, and an OFFSET relative to the start of that input
  // section, return whether or not the corresponding offset within
  // the output section is known.  If this function returns true, it
  // sets *POUTPUT to the output offset.  The value -1 indicates that
  // this input offset is being discarded.
  bool
  output_offset(const Relobj* object, unsigned int shndx,
		section_offset_type offset,
		section_offset_type *poutput) const
  { return this->do_output_offset(object, shndx, offset, poutput); }

  // Return whether this is the merge section for the input section
  // SHNDX in OBJECT.  This should return true when output_offset
  // would return true for some values of OFFSET.
  bool
  is_merge_section_for(const Relobj* object, unsigned int shndx) const
  { return this->do_is_merge_section_for(object, shndx); }

  // Write the contents to a buffer.  This is used for sections which
  // require postprocessing, such as compression.
  void
  write_to_buffer(unsigned char* buffer)
  { this->do_write_to_buffer(buffer); }

  // Print merge stats to stderr.  This should only be called for
  // SHF_MERGE sections.
  void
  print_merge_stats(const char* section_name)
  { this->do_print_merge_stats(section_name); }

 protected:
  // The child class must implement do_write.

  // The child class may implement specific adjustments to the output
  // section.
  virtual void
  do_adjust_output_section(Output_section*)
  { }

  // May be implemented by child class.  Return true if the section
  // was handled.
  virtual bool
  do_add_input_section(Relobj*, unsigned int)
  { gold_unreachable(); }

  // The child class may implement output_offset.
  virtual bool
  do_output_offset(const Relobj*, unsigned int, section_offset_type,
		   section_offset_type*) const
  { return false; }

  // The child class may implement is_merge_section_for.
  virtual bool
  do_is_merge_section_for(const Relobj*, unsigned int) const
  { return false; }

  // The child class may implement write_to_buffer.  Most child
  // classes can not appear in a compressed section, and they do not
  // implement this.
  virtual void
  do_write_to_buffer(unsigned char*)
  { gold_unreachable(); }

  // Print merge statistics.
  virtual void
  do_print_merge_stats(const char*)
  { gold_unreachable(); }

  // Return the required alignment.
  uint64_t
  do_addralign() const
  { return this->addralign_; }

  // Return the output section.
  Output_section*
  do_output_section()
  { return this->output_section_; }

  // Return the section index of the output section.
  unsigned int
  do_out_shndx() const;

  // Set the alignment.
  void
  set_addralign(uint64_t addralign);

 private:
  // The output section for this section.
  Output_section* output_section_;
  // The required alignment.
  uint64_t addralign_;
};

// Some Output_section_data classes build up their data step by step,
// rather than all at once.  This class provides an interface for
// them.

class Output_section_data_build : public Output_section_data
{
 public:
  Output_section_data_build(uint64_t addralign)
    : Output_section_data(addralign)
  { }

  // Get the current data size.
  off_t
  current_data_size() const
  { return this->current_data_size_for_child(); }

  // Set the current data size.
  void
  set_current_data_size(off_t data_size)
  { this->set_current_data_size_for_child(data_size); }

 protected:
  // Set the final data size.
  virtual void
  set_final_data_size()
  { this->set_data_size(this->current_data_size_for_child()); }
};

// A simple case of Output_data in which we have constant data to
// output.

class Output_data_const : public Output_section_data
{
 public:
  Output_data_const(const std::string& data, uint64_t addralign)
    : Output_section_data(data.size(), addralign), data_(data)
  { }

  Output_data_const(const char* p, off_t len, uint64_t addralign)
    : Output_section_data(len, addralign), data_(p, len)
  { }

  Output_data_const(const unsigned char* p, off_t len, uint64_t addralign)
    : Output_section_data(len, addralign),
      data_(reinterpret_cast<const char*>(p), len)
  { }

 protected:
  // Write the data to the output file.
  void
  do_write(Output_file*);

  // Write the data to a buffer.
  void
  do_write_to_buffer(unsigned char* buffer)
  { memcpy(buffer, this->data_.data(), this->data_.size()); }

 private:
  std::string data_;
};

// Another version of Output_data with constant data, in which the
// buffer is allocated by the caller.

class Output_data_const_buffer : public Output_section_data
{
 public:
  Output_data_const_buffer(const unsigned char* p, off_t len,
			   uint64_t addralign)
    : Output_section_data(len, addralign), p_(p)
  { }

 protected:
  // Write the data the output file.
  void
  do_write(Output_file*);

  // Write the data to a buffer.
  void
  do_write_to_buffer(unsigned char* buffer)
  { memcpy(buffer, this->p_, this->data_size()); }

 private:
  const unsigned char* p_;
};

// A place holder for a fixed amount of data written out via some
// other mechanism.

class Output_data_fixed_space : public Output_section_data
{
 public:
  Output_data_fixed_space(off_t data_size, uint64_t addralign)
    : Output_section_data(data_size, addralign)
  { }

 protected:
  // Write out the data--the actual data must be written out
  // elsewhere.
  void
  do_write(Output_file*)
  { }
};

// A place holder for variable sized data written out via some other
// mechanism.

class Output_data_space : public Output_section_data_build
{
 public:
  explicit Output_data_space(uint64_t addralign)
    : Output_section_data_build(addralign)
  { }

  // Set the alignment.
  void
  set_space_alignment(uint64_t align)
  { this->set_addralign(align); }

 protected:
  // Write out the data--the actual data must be written out
  // elsewhere.
  void
  do_write(Output_file*)
  { }
};

// A string table which goes into an output section.

class Output_data_strtab : public Output_section_data
{
 public:
  Output_data_strtab(Stringpool* strtab)
    : Output_section_data(1), strtab_(strtab)
  { }

 protected:
  // This is called to set the address and file offset.  Here we make
  // sure that the Stringpool is finalized.
  void
  set_final_data_size();

  // Write out the data.
  void
  do_write(Output_file*);

  // Write the data to a buffer.
  void
  do_write_to_buffer(unsigned char* buffer)
  { this->strtab_->write_to_buffer(buffer, this->data_size()); }

 private:
  Stringpool* strtab_;
};

// This POD class is used to represent a single reloc in the output
// file.  This could be a private class within Output_data_reloc, but
// the templatization is complex enough that I broke it out into a
// separate class.  The class is templatized on either elfcpp::SHT_REL
// or elfcpp::SHT_RELA, and also on whether this is a dynamic
// relocation or an ordinary relocation.

// A relocation can be against a global symbol, a local symbol, a
// local section symbol, an output section, or the undefined symbol at
// index 0.  We represent the latter by using a NULL global symbol.

template<int sh_type, bool dynamic, int size, bool big_endian>
class Output_reloc;

template<bool dynamic, int size, bool big_endian>
class Output_reloc<elfcpp::SHT_REL, dynamic, size, big_endian>
{
 public:
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Addend;

  // An uninitialized entry.  We need this because we want to put
  // instances of this class into an STL container.
  Output_reloc()
    : local_sym_index_(INVALID_CODE)
  { }

  // We have a bunch of different constructors.  They come in pairs
  // depending on how the address of the relocation is specified.  It
  // can either be an offset in an Output_data or an offset in an
  // input section.

  // A reloc against a global symbol.

  Output_reloc(Symbol* gsym, unsigned int type, Output_data* od,
	       Address address, bool is_relative);

  Output_reloc(Symbol* gsym, unsigned int type, Relobj* relobj,
	       unsigned int shndx, Address address, bool is_relative);

  // A reloc against a local symbol or local section symbol.

  Output_reloc(Sized_relobj<size, big_endian>* relobj,
	       unsigned int local_sym_index, unsigned int type,
	       Output_data* od, Address address, bool is_relative,
               bool is_section_symbol);

  Output_reloc(Sized_relobj<size, big_endian>* relobj,
	       unsigned int local_sym_index, unsigned int type,
	       unsigned int shndx, Address address, bool is_relative,
               bool is_section_symbol);

  // A reloc against the STT_SECTION symbol of an output section.

  Output_reloc(Output_section* os, unsigned int type, Output_data* od,
	       Address address);

  Output_reloc(Output_section* os, unsigned int type, Relobj* relobj,
	       unsigned int shndx, Address address);

  // Return TRUE if this is a RELATIVE relocation.
  bool
  is_relative() const
  { return this->is_relative_; }

  // Return whether this is against a local section symbol.
  bool
  is_local_section_symbol() const
  {
    return (this->local_sym_index_ != GSYM_CODE
            && this->local_sym_index_ != SECTION_CODE
            && this->local_sym_index_ != INVALID_CODE
            && this->is_section_symbol_);
  }

  // For a local section symbol, return the offset of the input
  // section within the output section.  ADDEND is the addend being
  // applied to the input section.
  section_offset_type
  local_section_offset(Addend addend) const;

  // Get the value of the symbol referred to by a Rel relocation when
  // we are adding the given ADDEND.
  Address
  symbol_value(Addend addend) const;

  // Write the reloc entry to an output view.
  void
  write(unsigned char* pov) const;

  // Write the offset and info fields to Write_rel.
  template<typename Write_rel>
  void write_rel(Write_rel*) const;

 private:
  // Record that we need a dynamic symbol index.
  void
  set_needs_dynsym_index();

  // Return the symbol index.
  unsigned int
  get_symbol_index() const;

  // Codes for local_sym_index_.
  enum
  {
    // Global symbol.
    GSYM_CODE = -1U,
    // Output section.
    SECTION_CODE = -2U,
    // Invalid uninitialized entry.
    INVALID_CODE = -3U
  };

  union
  {
    // For a local symbol or local section symbol
    // (this->local_sym_index_ >= 0), the object.  We will never
    // generate a relocation against a local symbol in a dynamic
    // object; that doesn't make sense.  And our callers will always
    // be templatized, so we use Sized_relobj here.
    Sized_relobj<size, big_endian>* relobj;
    // For a global symbol (this->local_sym_index_ == GSYM_CODE, the
    // symbol.  If this is NULL, it indicates a relocation against the
    // undefined 0 symbol.
    Symbol* gsym;
    // For a relocation against an output section
    // (this->local_sym_index_ == SECTION_CODE), the output section.
    Output_section* os;
  } u1_;
  union
  {
    // If this->shndx_ is not INVALID CODE, the object which holds the
    // input section being used to specify the reloc address.
    Relobj* relobj;
    // If this->shndx_ is INVALID_CODE, the output data being used to
    // specify the reloc address.  This may be NULL if the reloc
    // address is absolute.
    Output_data* od;
  } u2_;
  // The address offset within the input section or the Output_data.
  Address address_;
  // This is GSYM_CODE for a global symbol, or SECTION_CODE for a
  // relocation against an output section, or INVALID_CODE for an
  // uninitialized value.  Otherwise, for a local symbol
  // (this->is_section_symbol_ is false), the local symbol index.  For
  // a local section symbol (this->is_section_symbol_ is true), the
  // section index in the input file.
  unsigned int local_sym_index_;
  // The reloc type--a processor specific code.
  unsigned int type_ : 30;
  // True if the relocation is a RELATIVE relocation.
  bool is_relative_ : 1;
  // True if the relocation is against a section symbol.
  bool is_section_symbol_ : 1;
  // If the reloc address is an input section in an object, the
  // section index.  This is INVALID_CODE if the reloc address is
  // specified in some other way.
  unsigned int shndx_;
};

// The SHT_RELA version of Output_reloc<>.  This is just derived from
// the SHT_REL version of Output_reloc, but it adds an addend.

template<bool dynamic, int size, bool big_endian>
class Output_reloc<elfcpp::SHT_RELA, dynamic, size, big_endian>
{
 public:
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Addend;

  // An uninitialized entry.
  Output_reloc()
    : rel_()
  { }

  // A reloc against a global symbol.

  Output_reloc(Symbol* gsym, unsigned int type, Output_data* od,
	       Address address, Addend addend, bool is_relative)
    : rel_(gsym, type, od, address, is_relative), addend_(addend)
  { }

  Output_reloc(Symbol* gsym, unsigned int type, Relobj* relobj,
	       unsigned int shndx, Address address, Addend addend,
	       bool is_relative)
    : rel_(gsym, type, relobj, shndx, address, is_relative), addend_(addend)
  { }

  // A reloc against a local symbol.

  Output_reloc(Sized_relobj<size, big_endian>* relobj,
	       unsigned int local_sym_index, unsigned int type,
	       Output_data* od, Address address,
	       Addend addend, bool is_relative, bool is_section_symbol)
    : rel_(relobj, local_sym_index, type, od, address, is_relative,
           is_section_symbol),
      addend_(addend)
  { }

  Output_reloc(Sized_relobj<size, big_endian>* relobj,
	       unsigned int local_sym_index, unsigned int type,
	       unsigned int shndx, Address address,
	       Addend addend, bool is_relative, bool is_section_symbol)
    : rel_(relobj, local_sym_index, type, shndx, address, is_relative,
           is_section_symbol),
      addend_(addend)
  { }

  // A reloc against the STT_SECTION symbol of an output section.

  Output_reloc(Output_section* os, unsigned int type, Output_data* od,
	       Address address, Addend addend)
    : rel_(os, type, od, address), addend_(addend)
  { }

  Output_reloc(Output_section* os, unsigned int type, Relobj* relobj,
	       unsigned int shndx, Address address, Addend addend)
    : rel_(os, type, relobj, shndx, address), addend_(addend)
  { }

  // Write the reloc entry to an output view.
  void
  write(unsigned char* pov) const;

 private:
  // The basic reloc.
  Output_reloc<elfcpp::SHT_REL, dynamic, size, big_endian> rel_;
  // The addend.
  Addend addend_;
};

// Output_data_reloc is used to manage a section containing relocs.
// SH_TYPE is either elfcpp::SHT_REL or elfcpp::SHT_RELA.  DYNAMIC
// indicates whether this is a dynamic relocation or a normal
// relocation.  Output_data_reloc_base is a base class.
// Output_data_reloc is the real class, which we specialize based on
// the reloc type.

template<int sh_type, bool dynamic, int size, bool big_endian>
class Output_data_reloc_base : public Output_section_data_build
{
 public:
  typedef Output_reloc<sh_type, dynamic, size, big_endian> Output_reloc_type;
  typedef typename Output_reloc_type::Address Address;
  static const int reloc_size =
    Reloc_types<sh_type, size, big_endian>::reloc_size;

  // Construct the section.
  Output_data_reloc_base()
    : Output_section_data_build(Output_data::default_alignment_for_size(size))
  { }

 protected:
  // Write out the data.
  void
  do_write(Output_file*);

  // Set the entry size and the link.
  void
  do_adjust_output_section(Output_section *os);

  // Add a relocation entry.
  void
  add(Output_data *od, const Output_reloc_type& reloc)
  {
    this->relocs_.push_back(reloc);
    this->set_current_data_size(this->relocs_.size() * reloc_size);
    od->add_dynamic_reloc();
  }

 private:
  typedef std::vector<Output_reloc_type> Relocs;

  Relocs relocs_;
};

// The class which callers actually create.

template<int sh_type, bool dynamic, int size, bool big_endian>
class Output_data_reloc;

// The SHT_REL version of Output_data_reloc.

template<bool dynamic, int size, bool big_endian>
class Output_data_reloc<elfcpp::SHT_REL, dynamic, size, big_endian>
  : public Output_data_reloc_base<elfcpp::SHT_REL, dynamic, size, big_endian>
{
 private:
  typedef Output_data_reloc_base<elfcpp::SHT_REL, dynamic, size,
				 big_endian> Base;

 public:
  typedef typename Base::Output_reloc_type Output_reloc_type;
  typedef typename Output_reloc_type::Address Address;

  Output_data_reloc()
    : Output_data_reloc_base<elfcpp::SHT_REL, dynamic, size, big_endian>()
  { }

  // Add a reloc against a global symbol.

  void
  add_global(Symbol* gsym, unsigned int type, Output_data* od, Address address)
  { this->add(od, Output_reloc_type(gsym, type, od, address, false)); }

  void
  add_global(Symbol* gsym, unsigned int type, Output_data* od, Relobj* relobj,
	     unsigned int shndx, Address address)
  { this->add(od, Output_reloc_type(gsym, type, relobj, shndx, address,
                                    false)); }

  // Add a RELATIVE reloc against a global symbol.  The final relocation
  // will not reference the symbol.

  void
  add_global_relative(Symbol* gsym, unsigned int type, Output_data* od,
                      Address address)
  { this->add(od, Output_reloc_type(gsym, type, od, address, true)); }

  void
  add_global_relative(Symbol* gsym, unsigned int type, Output_data* od,
                      Relobj* relobj, unsigned int shndx, Address address)
  {
    this->add(od, Output_reloc_type(gsym, type, relobj, shndx, address,
                                    true));
  }

  // Add a reloc against a local symbol.

  void
  add_local(Sized_relobj<size, big_endian>* relobj,
	    unsigned int local_sym_index, unsigned int type,
	    Output_data* od, Address address)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, od,
                                    address, false, false));
  }

  void
  add_local(Sized_relobj<size, big_endian>* relobj,
	    unsigned int local_sym_index, unsigned int type,
	    Output_data* od, unsigned int shndx, Address address)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, shndx,
				    address, false, false));
  }

  // Add a RELATIVE reloc against a local symbol.

  void
  add_local_relative(Sized_relobj<size, big_endian>* relobj,
	             unsigned int local_sym_index, unsigned int type,
	             Output_data* od, Address address)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, od,
                                    address, true, false));
  }

  void
  add_local_relative(Sized_relobj<size, big_endian>* relobj,
	             unsigned int local_sym_index, unsigned int type,
	             Output_data* od, unsigned int shndx, Address address)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, shndx,
				    address, true, false));
  }

  // Add a reloc against a local section symbol.  This will be
  // converted into a reloc against the STT_SECTION symbol of the
  // output section.

  void
  add_local_section(Sized_relobj<size, big_endian>* relobj,
                    unsigned int input_shndx, unsigned int type,
                    Output_data* od, Address address)
  {
    this->add(od, Output_reloc_type(relobj, input_shndx, type, od,
                                    address, false, true));
  }

  void
  add_local_section(Sized_relobj<size, big_endian>* relobj,
                    unsigned int input_shndx, unsigned int type,
                    Output_data* od, unsigned int shndx, Address address)
  {
    this->add(od, Output_reloc_type(relobj, input_shndx, type, shndx,
                                    address, false, true));
  }

  // A reloc against the STT_SECTION symbol of an output section.
  // OS is the Output_section that the relocation refers to; OD is
  // the Output_data object being relocated.

  void
  add_output_section(Output_section* os, unsigned int type,
		     Output_data* od, Address address)
  { this->add(od, Output_reloc_type(os, type, od, address)); }

  void
  add_output_section(Output_section* os, unsigned int type, Output_data* od,
		     Relobj* relobj, unsigned int shndx, Address address)
  { this->add(od, Output_reloc_type(os, type, relobj, shndx, address)); }
};

// The SHT_RELA version of Output_data_reloc.

template<bool dynamic, int size, bool big_endian>
class Output_data_reloc<elfcpp::SHT_RELA, dynamic, size, big_endian>
  : public Output_data_reloc_base<elfcpp::SHT_RELA, dynamic, size, big_endian>
{
 private:
  typedef Output_data_reloc_base<elfcpp::SHT_RELA, dynamic, size,
				 big_endian> Base;

 public:
  typedef typename Base::Output_reloc_type Output_reloc_type;
  typedef typename Output_reloc_type::Address Address;
  typedef typename Output_reloc_type::Addend Addend;

  Output_data_reloc()
    : Output_data_reloc_base<elfcpp::SHT_RELA, dynamic, size, big_endian>()
  { }

  // Add a reloc against a global symbol.

  void
  add_global(Symbol* gsym, unsigned int type, Output_data* od,
	     Address address, Addend addend)
  { this->add(od, Output_reloc_type(gsym, type, od, address, addend,
                                    false)); }

  void
  add_global(Symbol* gsym, unsigned int type, Output_data* od, Relobj* relobj,
	     unsigned int shndx, Address address,
	     Addend addend)
  { this->add(od, Output_reloc_type(gsym, type, relobj, shndx, address,
                                    addend, false)); }

  // Add a RELATIVE reloc against a global symbol.  The final output
  // relocation will not reference the symbol, but we must keep the symbol
  // information long enough to set the addend of the relocation correctly
  // when it is written.

  void
  add_global_relative(Symbol* gsym, unsigned int type, Output_data* od,
	              Address address, Addend addend)
  { this->add(od, Output_reloc_type(gsym, type, od, address, addend, true)); }

  void
  add_global_relative(Symbol* gsym, unsigned int type, Output_data* od,
                      Relobj* relobj, unsigned int shndx, Address address,
	              Addend addend)
  { this->add(od, Output_reloc_type(gsym, type, relobj, shndx, address,
                                    addend, true)); }

  // Add a reloc against a local symbol.

  void
  add_local(Sized_relobj<size, big_endian>* relobj,
	    unsigned int local_sym_index, unsigned int type,
	    Output_data* od, Address address, Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, od, address,
				    addend, false, false));
  }

  void
  add_local(Sized_relobj<size, big_endian>* relobj,
	    unsigned int local_sym_index, unsigned int type,
	    Output_data* od, unsigned int shndx, Address address,
	    Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, shndx,
                                    address, addend, false, false));
  }

  // Add a RELATIVE reloc against a local symbol.

  void
  add_local_relative(Sized_relobj<size, big_endian>* relobj,
	             unsigned int local_sym_index, unsigned int type,
	             Output_data* od, Address address, Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, od, address,
				    addend, true, false));
  }

  void
  add_local_relative(Sized_relobj<size, big_endian>* relobj,
	             unsigned int local_sym_index, unsigned int type,
	             Output_data* od, unsigned int shndx, Address address,
	             Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, local_sym_index, type, shndx,
                                    address, addend, true, false));
  }

  // Add a reloc against a local section symbol.  This will be
  // converted into a reloc against the STT_SECTION symbol of the
  // output section.

  void
  add_local_section(Sized_relobj<size, big_endian>* relobj,
                    unsigned int input_shndx, unsigned int type,
                    Output_data* od, Address address, Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, input_shndx, type, od, address,
				    addend, false, true));
  }

  void
  add_local_section(Sized_relobj<size, big_endian>* relobj,
	             unsigned int input_shndx, unsigned int type,
	             Output_data* od, unsigned int shndx, Address address,
	             Addend addend)
  {
    this->add(od, Output_reloc_type(relobj, input_shndx, type, shndx,
                                    address, addend, false, true));
  }

  // A reloc against the STT_SECTION symbol of an output section.

  void
  add_output_section(Output_section* os, unsigned int type, Output_data* od,
		     Address address, Addend addend)
  { this->add(os, Output_reloc_type(os, type, od, address, addend)); }

  void
  add_output_section(Output_section* os, unsigned int type, Relobj* relobj,
		     unsigned int shndx, Address address, Addend addend)
  { this->add(os, Output_reloc_type(os, type, relobj, shndx, address,
                                    addend)); }
};

// Output_relocatable_relocs represents a relocation section in a
// relocatable link.  The actual data is written out in the target
// hook relocate_for_relocatable.  This just saves space for it.

template<int sh_type, int size, bool big_endian>
class Output_relocatable_relocs : public Output_section_data
{
 public:
  Output_relocatable_relocs(Relocatable_relocs* rr)
    : Output_section_data(Output_data::default_alignment_for_size(size)),
      rr_(rr)
  { }

  void
  set_final_data_size();

  // Write out the data.  There is nothing to do here.
  void
  do_write(Output_file*)
  { }

 private:
  // The relocs associated with this input section.
  Relocatable_relocs* rr_;
};

// Handle a GROUP section.

template<int size, bool big_endian>
class Output_data_group : public Output_section_data
{
 public:
  Output_data_group(Sized_relobj<size, big_endian>* relobj,
		    section_size_type entry_count,
		    const elfcpp::Elf_Word* contents);

  void
  do_write(Output_file*);

 private:
  // The input object.
  Sized_relobj<size, big_endian>* relobj_;
  // The group flag word.
  elfcpp::Elf_Word flags_;
  // The section indexes of the input sections in this group.
  std::vector<unsigned int> input_sections_;
};

// Output_data_got is used to manage a GOT.  Each entry in the GOT is
// for one symbol--either a global symbol or a local symbol in an
// object.  The target specific code adds entries to the GOT as
// needed.

template<int size, bool big_endian>
class Output_data_got : public Output_section_data_build
{
 public:
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Valtype;
  typedef Output_data_reloc<elfcpp::SHT_REL, true, size, big_endian> Rel_dyn;
  typedef Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian> Rela_dyn;

  Output_data_got()
    : Output_section_data_build(Output_data::default_alignment_for_size(size)),
      entries_()
  { }

  // Add an entry for a global symbol to the GOT.  Return true if this
  // is a new GOT entry, false if the symbol was already in the GOT.
  bool
  add_global(Symbol* gsym, unsigned int got_type);

  // Add an entry for a global symbol to the GOT, and add a dynamic
  // relocation of type R_TYPE for the GOT entry.
  void
  add_global_with_rel(Symbol* gsym, unsigned int got_type,
                      Rel_dyn* rel_dyn, unsigned int r_type);

  void
  add_global_with_rela(Symbol* gsym, unsigned int got_type,
                       Rela_dyn* rela_dyn, unsigned int r_type);

  // Add a pair of entries for a global symbol to the GOT, and add
  // dynamic relocations of type R_TYPE_1 and R_TYPE_2, respectively.
  void
  add_global_pair_with_rel(Symbol* gsym, unsigned int got_type,
                           Rel_dyn* rel_dyn, unsigned int r_type_1,
                           unsigned int r_type_2);

  void
  add_global_pair_with_rela(Symbol* gsym, unsigned int got_type,
                            Rela_dyn* rela_dyn, unsigned int r_type_1,
                            unsigned int r_type_2);

  // Add an entry for a local symbol to the GOT.  This returns true if
  // this is a new GOT entry, false if the symbol already has a GOT
  // entry.
  bool
  add_local(Sized_relobj<size, big_endian>* object, unsigned int sym_index,
            unsigned int got_type);

  // Add an entry for a local symbol to the GOT, and add a dynamic
  // relocation of type R_TYPE for the GOT entry.
  void
  add_local_with_rel(Sized_relobj<size, big_endian>* object,
                     unsigned int sym_index, unsigned int got_type,
                     Rel_dyn* rel_dyn, unsigned int r_type);

  void
  add_local_with_rela(Sized_relobj<size, big_endian>* object,
                      unsigned int sym_index, unsigned int got_type,
                      Rela_dyn* rela_dyn, unsigned int r_type);

  // Add a pair of entries for a local symbol to the GOT, and add
  // dynamic relocations of type R_TYPE_1 and R_TYPE_2, respectively.
  void
  add_local_pair_with_rel(Sized_relobj<size, big_endian>* object,
                          unsigned int sym_index, unsigned int shndx,
                          unsigned int got_type, Rel_dyn* rel_dyn,
                          unsigned int r_type_1, unsigned int r_type_2);

  void
  add_local_pair_with_rela(Sized_relobj<size, big_endian>* object,
                          unsigned int sym_index, unsigned int shndx,
                          unsigned int got_type, Rela_dyn* rela_dyn,
                          unsigned int r_type_1, unsigned int r_type_2);

  // Add a constant to the GOT.  This returns the offset of the new
  // entry from the start of the GOT.
  unsigned int
  add_constant(Valtype constant)
  {
    this->entries_.push_back(Got_entry(constant));
    this->set_got_size();
    return this->last_got_offset();
  }

 protected:
  // Write out the GOT table.
  void
  do_write(Output_file*);

 private:
  // This POD class holds a single GOT entry.
  class Got_entry
  {
   public:
    // Create a zero entry.
    Got_entry()
      : local_sym_index_(CONSTANT_CODE)
    { this->u_.constant = 0; }

    // Create a global symbol entry.
    explicit Got_entry(Symbol* gsym)
      : local_sym_index_(GSYM_CODE)
    { this->u_.gsym = gsym; }

    // Create a local symbol entry.
    Got_entry(Sized_relobj<size, big_endian>* object,
              unsigned int local_sym_index)
      : local_sym_index_(local_sym_index)
    {
      gold_assert(local_sym_index != GSYM_CODE
		  && local_sym_index != CONSTANT_CODE);
      this->u_.object = object;
    }

    // Create a constant entry.  The constant is a host value--it will
    // be swapped, if necessary, when it is written out.
    explicit Got_entry(Valtype constant)
      : local_sym_index_(CONSTANT_CODE)
    { this->u_.constant = constant; }

    // Write the GOT entry to an output view.
    void
    write(unsigned char* pov) const;

   private:
    enum
    {
      GSYM_CODE = -1U,
      CONSTANT_CODE = -2U
    };

    union
    {
      // For a local symbol, the object.
      Sized_relobj<size, big_endian>* object;
      // For a global symbol, the symbol.
      Symbol* gsym;
      // For a constant, the constant.
      Valtype constant;
    } u_;
    // For a local symbol, the local symbol index.  This is GSYM_CODE
    // for a global symbol, or CONSTANT_CODE for a constant.
    unsigned int local_sym_index_;
  };

  typedef std::vector<Got_entry> Got_entries;

  // Return the offset into the GOT of GOT entry I.
  unsigned int
  got_offset(unsigned int i) const
  { return i * (size / 8); }

  // Return the offset into the GOT of the last entry added.
  unsigned int
  last_got_offset() const
  { return this->got_offset(this->entries_.size() - 1); }

  // Set the size of the section.
  void
  set_got_size()
  { this->set_current_data_size(this->got_offset(this->entries_.size())); }

  // The list of GOT entries.
  Got_entries entries_;
};

// Output_data_dynamic is used to hold the data in SHT_DYNAMIC
// section.

class Output_data_dynamic : public Output_section_data
{
 public:
  Output_data_dynamic(Stringpool* pool)
    : Output_section_data(Output_data::default_alignment()),
      entries_(), pool_(pool)
  { }

  // Add a new dynamic entry with a fixed numeric value.
  void
  add_constant(elfcpp::DT tag, unsigned int val)
  { this->add_entry(Dynamic_entry(tag, val)); }

  // Add a new dynamic entry with the address of output data.
  void
  add_section_address(elfcpp::DT tag, const Output_data* od)
  { this->add_entry(Dynamic_entry(tag, od, false)); }

  // Add a new dynamic entry with the address of output data
  // plus a constant offset.
  void
  add_section_plus_offset(elfcpp::DT tag, const Output_data* od,
                          unsigned int offset)
  { this->add_entry(Dynamic_entry(tag, od, offset)); }

  // Add a new dynamic entry with the size of output data.
  void
  add_section_size(elfcpp::DT tag, const Output_data* od)
  { this->add_entry(Dynamic_entry(tag, od, true)); }

  // Add a new dynamic entry with the address of a symbol.
  void
  add_symbol(elfcpp::DT tag, const Symbol* sym)
  { this->add_entry(Dynamic_entry(tag, sym)); }

  // Add a new dynamic entry with a string.
  void
  add_string(elfcpp::DT tag, const char* str)
  { this->add_entry(Dynamic_entry(tag, this->pool_->add(str, true, NULL))); }

  void
  add_string(elfcpp::DT tag, const std::string& str)
  { this->add_string(tag, str.c_str()); }

 protected:
  // Adjust the output section to set the entry size.
  void
  do_adjust_output_section(Output_section*);

  // Set the final data size.
  void
  set_final_data_size();

  // Write out the dynamic entries.
  void
  do_write(Output_file*);

 private:
  // This POD class holds a single dynamic entry.
  class Dynamic_entry
  {
   public:
    // Create an entry with a fixed numeric value.
    Dynamic_entry(elfcpp::DT tag, unsigned int val)
      : tag_(tag), offset_(DYNAMIC_NUMBER)
    { this->u_.val = val; }

    // Create an entry with the size or address of a section.
    Dynamic_entry(elfcpp::DT tag, const Output_data* od, bool section_size)
      : tag_(tag),
	offset_(section_size
		? DYNAMIC_SECTION_SIZE
		: DYNAMIC_SECTION_ADDRESS)
    { this->u_.od = od; }

    // Create an entry with the address of a section plus a constant offset.
    Dynamic_entry(elfcpp::DT tag, const Output_data* od, unsigned int offset)
      : tag_(tag),
	offset_(offset)
    { this->u_.od = od; }

    // Create an entry with the address of a symbol.
    Dynamic_entry(elfcpp::DT tag, const Symbol* sym)
      : tag_(tag), offset_(DYNAMIC_SYMBOL)
    { this->u_.sym = sym; }

    // Create an entry with a string.
    Dynamic_entry(elfcpp::DT tag, const char* str)
      : tag_(tag), offset_(DYNAMIC_STRING)
    { this->u_.str = str; }

    // Write the dynamic entry to an output view.
    template<int size, bool big_endian>
    void
    write(unsigned char* pov, const Stringpool*) const;

   private:
    // Classification is encoded in the OFFSET field.
    enum Classification
    {
      // Section address.
      DYNAMIC_SECTION_ADDRESS = 0,
      // Number.
      DYNAMIC_NUMBER = -1U,
      // Section size.
      DYNAMIC_SECTION_SIZE = -2U,
      // Symbol adress.
      DYNAMIC_SYMBOL = -3U,
      // String.
      DYNAMIC_STRING = -4U
      // Any other value indicates a section address plus OFFSET.
    };

    union
    {
      // For DYNAMIC_NUMBER.
      unsigned int val;
      // For DYNAMIC_SECTION_SIZE and section address plus OFFSET.
      const Output_data* od;
      // For DYNAMIC_SYMBOL.
      const Symbol* sym;
      // For DYNAMIC_STRING.
      const char* str;
    } u_;
    // The dynamic tag.
    elfcpp::DT tag_;
    // The type of entry (Classification) or offset within a section.
    unsigned int offset_;
  };

  // Add an entry to the list.
  void
  add_entry(const Dynamic_entry& entry)
  { this->entries_.push_back(entry); }

  // Sized version of write function.
  template<int size, bool big_endian>
  void
  sized_write(Output_file* of);

  // The type of the list of entries.
  typedef std::vector<Dynamic_entry> Dynamic_entries;

  // The entries.
  Dynamic_entries entries_;
  // The pool used for strings.
  Stringpool* pool_;
};

// An output section.  We don't expect to have too many output
// sections, so we don't bother to do a template on the size.

class Output_section : public Output_data
{
 public:
  // Create an output section, giving the name, type, and flags.
  Output_section(const char* name, elfcpp::Elf_Word, elfcpp::Elf_Xword);
  virtual ~Output_section();

  // Add a new input section SHNDX, named NAME, with header SHDR, from
  // object OBJECT.  RELOC_SHNDX is the index of a relocation section
  // which applies to this section, or 0 if none, or -1U if more than
  // one.  HAVE_SECTIONS_SCRIPT is true if we have a SECTIONS clause
  // in a linker script; in that case we need to keep track of input
  // sections associated with an output section.  Return the offset
  // within the output section.
  template<int size, bool big_endian>
  off_t
  add_input_section(Sized_relobj<size, big_endian>* object, unsigned int shndx,
		    const char *name,
		    const elfcpp::Shdr<size, big_endian>& shdr,
		    unsigned int reloc_shndx, bool have_sections_script);

  // Add generated data POSD to this output section.
  void
  add_output_section_data(Output_section_data* posd);

  // Return the section name.
  const char*
  name() const
  { return this->name_; }

  // Return the section type.
  elfcpp::Elf_Word
  type() const
  { return this->type_; }

  // Return the section flags.
  elfcpp::Elf_Xword
  flags() const
  { return this->flags_; }

  // Set the section flags.  This may only be used with the Layout
  // code when it is prepared to move the section to a different
  // segment.
  void
  set_flags(elfcpp::Elf_Xword flags)
  { this->flags_ = flags; }

  // Update the output section flags based on input section flags.
  void
  update_flags_for_input_section(elfcpp::Elf_Xword flags)
  {
    this->flags_ |= (flags
		     & (elfcpp::SHF_WRITE
			| elfcpp::SHF_ALLOC
			| elfcpp::SHF_EXECINSTR));
  }

  // Return the entsize field.
  uint64_t
  entsize() const
  { return this->entsize_; }

  // Set the entsize field.
  void
  set_entsize(uint64_t v);

  // Set the load address.
  void
  set_load_address(uint64_t load_address)
  {
    this->load_address_ = load_address;
    this->has_load_address_ = true;
  }

  // Set the link field to the output section index of a section.
  void
  set_link_section(const Output_data* od)
  {
    gold_assert(this->link_ == 0
		&& !this->should_link_to_symtab_
		&& !this->should_link_to_dynsym_);
    this->link_section_ = od;
  }

  // Set the link field to a constant.
  void
  set_link(unsigned int v)
  {
    gold_assert(this->link_section_ == NULL
		&& !this->should_link_to_symtab_
		&& !this->should_link_to_dynsym_);
    this->link_ = v;
  }

  // Record that this section should link to the normal symbol table.
  void
  set_should_link_to_symtab()
  {
    gold_assert(this->link_section_ == NULL
		&& this->link_ == 0
		&& !this->should_link_to_dynsym_);
    this->should_link_to_symtab_ = true;
  }

  // Record that this section should link to the dynamic symbol table.
  void
  set_should_link_to_dynsym()
  {
    gold_assert(this->link_section_ == NULL
		&& this->link_ == 0
		&& !this->should_link_to_symtab_);
    this->should_link_to_dynsym_ = true;
  }

  // Return the info field.
  unsigned int
  info() const
  {
    gold_assert(this->info_section_ == NULL
		&& this->info_symndx_ == NULL);
    return this->info_;
  }

  // Set the info field to the output section index of a section.
  void
  set_info_section(const Output_section* os)
  {
    gold_assert((this->info_section_ == NULL
		 || (this->info_section_ == os
		     && this->info_uses_section_index_))
		&& this->info_symndx_ == NULL
		&& this->info_ == 0);
    this->info_section_ = os;
    this->info_uses_section_index_= true;
  }

  // Set the info field to the symbol table index of a symbol.
  void
  set_info_symndx(const Symbol* sym)
  {
    gold_assert(this->info_section_ == NULL
		&& (this->info_symndx_ == NULL
		    || this->info_symndx_ == sym)
		&& this->info_ == 0);
    this->info_symndx_ = sym;
  }

  // Set the info field to the symbol table index of a section symbol.
  void
  set_info_section_symndx(const Output_section* os)
  {
    gold_assert((this->info_section_ == NULL
		 || (this->info_section_ == os
		     && !this->info_uses_section_index_))
		&& this->info_symndx_ == NULL
		&& this->info_ == 0);
    this->info_section_ = os;
    this->info_uses_section_index_ = false;
  }

  // Set the info field to a constant.
  void
  set_info(unsigned int v)
  {
    gold_assert(this->info_section_ == NULL
		&& this->info_symndx_ == NULL
		&& (this->info_ == 0
		    || this->info_ == v));
    this->info_ = v;
  }

  // Set the addralign field.
  void
  set_addralign(uint64_t v)
  { this->addralign_ = v; }

  // Indicate that we need a symtab index.
  void
  set_needs_symtab_index()
  { this->needs_symtab_index_ = true; }

  // Return whether we need a symtab index.
  bool
  needs_symtab_index() const
  { return this->needs_symtab_index_; }

  // Get the symtab index.
  unsigned int
  symtab_index() const
  {
    gold_assert(this->symtab_index_ != 0);
    return this->symtab_index_;
  }

  // Set the symtab index.
  void
  set_symtab_index(unsigned int index)
  {
    gold_assert(index != 0);
    this->symtab_index_ = index;
  }

  // Indicate that we need a dynsym index.
  void
  set_needs_dynsym_index()
  { this->needs_dynsym_index_ = true; }

  // Return whether we need a dynsym index.
  bool
  needs_dynsym_index() const
  { return this->needs_dynsym_index_; }

  // Get the dynsym index.
  unsigned int
  dynsym_index() const
  {
    gold_assert(this->dynsym_index_ != 0);
    return this->dynsym_index_;
  }

  // Set the dynsym index.
  void
  set_dynsym_index(unsigned int index)
  {
    gold_assert(index != 0);
    this->dynsym_index_ = index;
  }

  // Return whether the input sections sections attachd to this output
  // section may require sorting.  This is used to handle constructor
  // priorities compatibly with GNU ld.
  bool
  may_sort_attached_input_sections() const
  { return this->may_sort_attached_input_sections_; }

  // Record that the input sections attached to this output section
  // may require sorting.
  void
  set_may_sort_attached_input_sections()
  { this->may_sort_attached_input_sections_ = true; }

  // Return whether the input sections attached to this output section
  // require sorting.  This is used to handle constructor priorities
  // compatibly with GNU ld.
  bool
  must_sort_attached_input_sections() const
  { return this->must_sort_attached_input_sections_; }

  // Record that the input sections attached to this output section
  // require sorting.
  void
  set_must_sort_attached_input_sections()
  { this->must_sort_attached_input_sections_ = true; }

  // Return whether this section should be written after all the input
  // sections are complete.
  bool
  after_input_sections() const
  { return this->after_input_sections_; }

  // Record that this section should be written after all the input
  // sections are complete.
  void
  set_after_input_sections()
  { this->after_input_sections_ = true; }

  // Return whether this section requires postprocessing after all
  // relocations have been applied.
  bool
  requires_postprocessing() const
  { return this->requires_postprocessing_; }

  // If a section requires postprocessing, return the buffer to use.
  unsigned char*
  postprocessing_buffer() const
  {
    gold_assert(this->postprocessing_buffer_ != NULL);
    return this->postprocessing_buffer_;
  }

  // If a section requires postprocessing, create the buffer to use.
  void
  create_postprocessing_buffer();

  // If a section requires postprocessing, this is the size of the
  // buffer to which relocations should be applied.
  off_t
  postprocessing_buffer_size() const
  { return this->current_data_size_for_child(); }

  // Modify the section name.  This is only permitted for an
  // unallocated section, and only before the size has been finalized.
  // Otherwise the name will not get into Layout::namepool_.
  void
  set_name(const char* newname)
  {
    gold_assert((this->flags_ & elfcpp::SHF_ALLOC) == 0);
    gold_assert(!this->is_data_size_valid());
    this->name_ = newname;
  }

  // Return whether the offset OFFSET in the input section SHNDX in
  // object OBJECT is being included in the link.
  bool
  is_input_address_mapped(const Relobj* object, unsigned int shndx,
			  off_t offset) const;

  // Return the offset within the output section of OFFSET relative to
  // the start of input section SHNDX in object OBJECT.
  section_offset_type
  output_offset(const Relobj* object, unsigned int shndx,
		section_offset_type offset) const;

  // Return the output virtual address of OFFSET relative to the start
  // of input section SHNDX in object OBJECT.
  uint64_t
  output_address(const Relobj* object, unsigned int shndx,
		 off_t offset) const;

  // Return the output address of the start of the merged section for
  // input section SHNDX in object OBJECT.  This is not necessarily
  // the offset corresponding to input offset 0 in the section, since
  // the section may be mapped arbitrarily.
  uint64_t
  starting_output_address(const Relobj* object, unsigned int shndx) const;

  // Record that this output section was found in the SECTIONS clause
  // of a linker script.
  void
  set_found_in_sections_clause()
  { this->found_in_sections_clause_ = true; }

  // Return whether this output section was found in the SECTIONS
  // clause of a linker script.
  bool
  found_in_sections_clause() const
  { return this->found_in_sections_clause_; }

  // Write the section header into *OPHDR.
  template<int size, bool big_endian>
  void
  write_header(const Layout*, const Stringpool*,
	       elfcpp::Shdr_write<size, big_endian>*) const;

  // The next few calls are for linker script support.

  // Store the list of input sections for this Output_section into the
  // list passed in.  This removes the input sections, leaving only
  // any Output_section_data elements.  This returns the size of those
  // Output_section_data elements.  ADDRESS is the address of this
  // output section.  FILL is the fill value to use, in case there are
  // any spaces between the remaining Output_section_data elements.
  uint64_t
  get_input_sections(uint64_t address, const std::string& fill,
		     std::list<std::pair<Relobj*, unsigned int > >*);

  // Add an input section from a script.
  void
  add_input_section_for_script(Relobj* object, unsigned int shndx,
			       off_t data_size, uint64_t addralign);

  // Set the current size of the output section.
  void
  set_current_data_size(off_t size)
  { this->set_current_data_size_for_child(size); }

  // Get the current size of the output section.
  off_t
  current_data_size() const
  { return this->current_data_size_for_child(); }

  // End of linker script support.

  // Print merge statistics to stderr.
  void
  print_merge_stats();

 protected:
  // Return the output section--i.e., the object itself.
  Output_section*
  do_output_section()
  { return this; }

  // Return the section index in the output file.
  unsigned int
  do_out_shndx() const
  {
    gold_assert(this->out_shndx_ != -1U);
    return this->out_shndx_;
  }

  // Set the output section index.
  void
  do_set_out_shndx(unsigned int shndx)
  {
    gold_assert(this->out_shndx_ == -1U || this->out_shndx_ == shndx);
    this->out_shndx_ = shndx;
  }

  // Set the final data size of the Output_section.  For a typical
  // Output_section, there is nothing to do, but if there are any
  // Output_section_data objects we need to set their final addresses
  // here.
  virtual void
  set_final_data_size();

  // Reset the address and file offset.
  void
  do_reset_address_and_file_offset();

  // Write the data to the file.  For a typical Output_section, this
  // does nothing: the data is written out by calling Object::Relocate
  // on each input object.  But if there are any Output_section_data
  // objects we do need to write them out here.
  virtual void
  do_write(Output_file*);

  // Return the address alignment--function required by parent class.
  uint64_t
  do_addralign() const
  { return this->addralign_; }

  // Return whether there is a load address.
  bool
  do_has_load_address() const
  { return this->has_load_address_; }

  // Return the load address.
  uint64_t
  do_load_address() const
  {
    gold_assert(this->has_load_address_);
    return this->load_address_;
  }

  // Return whether this is an Output_section.
  bool
  do_is_section() const
  { return true; }

  // Return whether this is a section of the specified type.
  bool
  do_is_section_type(elfcpp::Elf_Word type) const
  { return this->type_ == type; }

  // Return whether the specified section flag is set.
  bool
  do_is_section_flag_set(elfcpp::Elf_Xword flag) const
  { return (this->flags_ & flag) != 0; }

  // Set the TLS offset.  Called only for SHT_TLS sections.
  void
  do_set_tls_offset(uint64_t tls_base);

  // Return the TLS offset, relative to the base of the TLS segment.
  // Valid only for SHT_TLS sections.
  uint64_t
  do_tls_offset() const
  { return this->tls_offset_; }

  // This may be implemented by a child class.
  virtual void
  do_finalize_name(Layout*)
  { }

  // Record that this section requires postprocessing after all
  // relocations have been applied.  This is called by a child class.
  void
  set_requires_postprocessing()
  {
    this->requires_postprocessing_ = true;
    this->after_input_sections_ = true;
  }

  // Write all the data of an Output_section into the postprocessing
  // buffer.
  void
  write_to_postprocessing_buffer();

 private:
  // In some cases we need to keep a list of the input sections
  // associated with this output section.  We only need the list if we
  // might have to change the offsets of the input section within the
  // output section after we add the input section.  The ordinary
  // input sections will be written out when we process the object
  // file, and as such we don't need to track them here.  We do need
  // to track Output_section_data objects here.  We store instances of
  // this structure in a std::vector, so it must be a POD.  There can
  // be many instances of this structure, so we use a union to save
  // some space.
  class Input_section
  {
   public:
    Input_section()
      : shndx_(0), p2align_(0)
    {
      this->u1_.data_size = 0;
      this->u2_.object = NULL;
    }

    // For an ordinary input section.
    Input_section(Relobj* object, unsigned int shndx, off_t data_size,
		  uint64_t addralign)
      : shndx_(shndx),
	p2align_(ffsll(static_cast<long long>(addralign)))
    {
      gold_assert(shndx != OUTPUT_SECTION_CODE
		  && shndx != MERGE_DATA_SECTION_CODE
		  && shndx != MERGE_STRING_SECTION_CODE);
      this->u1_.data_size = data_size;
      this->u2_.object = object;
    }

    // For a non-merge output section.
    Input_section(Output_section_data* posd)
      : shndx_(OUTPUT_SECTION_CODE),
	p2align_(ffsll(static_cast<long long>(posd->addralign())))
    {
      this->u1_.data_size = 0;
      this->u2_.posd = posd;
    }

    // For a merge section.
    Input_section(Output_section_data* posd, bool is_string, uint64_t entsize)
      : shndx_(is_string
	       ? MERGE_STRING_SECTION_CODE
	       : MERGE_DATA_SECTION_CODE),
	p2align_(ffsll(static_cast<long long>(posd->addralign())))
    {
      this->u1_.entsize = entsize;
      this->u2_.posd = posd;
    }

    // The required alignment.
    uint64_t
    addralign() const
    {
      return (this->p2align_ == 0
	      ? 0
	      : static_cast<uint64_t>(1) << (this->p2align_ - 1));
    }

    // Return the required size.
    off_t
    data_size() const;

    // Whether this is an input section.
    bool
    is_input_section() const
    {
      return (this->shndx_ != OUTPUT_SECTION_CODE
	      && this->shndx_ != MERGE_DATA_SECTION_CODE
	      && this->shndx_ != MERGE_STRING_SECTION_CODE);
    }

    // Return whether this is a merge section which matches the
    // parameters.
    bool
    is_merge_section(bool is_string, uint64_t entsize,
                     uint64_t addralign) const
    {
      return (this->shndx_ == (is_string
			       ? MERGE_STRING_SECTION_CODE
			       : MERGE_DATA_SECTION_CODE)
	      && this->u1_.entsize == entsize
              && this->addralign() == addralign);
    }

    // Return the object for an input section.
    Relobj*
    relobj() const
    {
      gold_assert(this->is_input_section());
      return this->u2_.object;
    }

    // Return the input section index for an input section.
    unsigned int
    shndx() const
    {
      gold_assert(this->is_input_section());
      return this->shndx_;
    }

    // Set the output section.
    void
    set_output_section(Output_section* os)
    {
      gold_assert(!this->is_input_section());
      this->u2_.posd->set_output_section(os);
    }

    // Set the address and file offset.  This is called during
    // Layout::finalize.  SECTION_FILE_OFFSET is the file offset of
    // the enclosing section.
    void
    set_address_and_file_offset(uint64_t address, off_t file_offset,
				off_t section_file_offset);

    // Reset the address and file offset.
    void
    reset_address_and_file_offset();

    // Finalize the data size.
    void
    finalize_data_size();

    // Add an input section, for SHF_MERGE sections.
    bool
    add_input_section(Relobj* object, unsigned int shndx)
    {
      gold_assert(this->shndx_ == MERGE_DATA_SECTION_CODE
		  || this->shndx_ == MERGE_STRING_SECTION_CODE);
      return this->u2_.posd->add_input_section(object, shndx);
    }

    // Given an input OBJECT, an input section index SHNDX within that
    // object, and an OFFSET relative to the start of that input
    // section, return whether or not the output offset is known.  If
    // this function returns true, it sets *POUTPUT to the offset in
    // the output section, relative to the start of the input section
    // in the output section.  *POUTPUT may be different from OFFSET
    // for a merged section.
    bool
    output_offset(const Relobj* object, unsigned int shndx,
		  section_offset_type offset,
		  section_offset_type *poutput) const;

    // Return whether this is the merge section for the input section
    // SHNDX in OBJECT.
    bool
    is_merge_section_for(const Relobj* object, unsigned int shndx) const;

    // Write out the data.  This does nothing for an input section.
    void
    write(Output_file*);

    // Write the data to a buffer.  This does nothing for an input
    // section.
    void
    write_to_buffer(unsigned char*);

    // Print statistics about merge sections to stderr.
    void
    print_merge_stats(const char* section_name)
    {
      if (this->shndx_ == MERGE_DATA_SECTION_CODE
	  || this->shndx_ == MERGE_STRING_SECTION_CODE)
	this->u2_.posd->print_merge_stats(section_name);
    }

   private:
    // Code values which appear in shndx_.  If the value is not one of
    // these codes, it is the input section index in the object file.
    enum
    {
      // An Output_section_data.
      OUTPUT_SECTION_CODE = -1U,
      // An Output_section_data for an SHF_MERGE section with
      // SHF_STRINGS not set.
      MERGE_DATA_SECTION_CODE = -2U,
      // An Output_section_data for an SHF_MERGE section with
      // SHF_STRINGS set.
      MERGE_STRING_SECTION_CODE = -3U
    };

    // For an ordinary input section, this is the section index in the
    // input file.  For an Output_section_data, this is
    // OUTPUT_SECTION_CODE or MERGE_DATA_SECTION_CODE or
    // MERGE_STRING_SECTION_CODE.
    unsigned int shndx_;
    // The required alignment, stored as a power of 2.
    unsigned int p2align_;
    union
    {
      // For an ordinary input section, the section size.
      off_t data_size;
      // For OUTPUT_SECTION_CODE, this is not used.  For
      // MERGE_DATA_SECTION_CODE or MERGE_STRING_SECTION_CODE, the
      // entity size.
      uint64_t entsize;
    } u1_;
    union
    {
      // For an ordinary input section, the object which holds the
      // input section.
      Relobj* object;
      // For OUTPUT_SECTION_CODE or MERGE_DATA_SECTION_CODE or
      // MERGE_STRING_SECTION_CODE, the data.
      Output_section_data* posd;
    } u2_;
  };

  typedef std::vector<Input_section> Input_section_list;

  // This class is used to sort the input sections.
  class Input_section_sort_entry;

  // This is the sort comparison function.
  struct Input_section_sort_compare
  {
    bool
    operator()(const Input_section_sort_entry&,
	       const Input_section_sort_entry&) const;
  };

  // Fill data.  This is used to fill in data between input sections.
  // It is also used for data statements (BYTE, WORD, etc.) in linker
  // scripts.  When we have to keep track of the input sections, we
  // can use an Output_data_const, but we don't want to have to keep
  // track of input sections just to implement fills.
  class Fill
  {
   public:
    Fill(off_t section_offset, off_t length)
      : section_offset_(section_offset),
	length_(convert_to_section_size_type(length))
    { }

    // Return section offset.
    off_t
    section_offset() const
    { return this->section_offset_; }

    // Return fill length.
    section_size_type
    length() const
    { return this->length_; }

   private:
    // The offset within the output section.
    off_t section_offset_;
    // The length of the space to fill.
    section_size_type length_;
  };

  typedef std::vector<Fill> Fill_list;

  // Add a new output section by Input_section.
  void
  add_output_section_data(Input_section*);

  // Add an SHF_MERGE input section.  Returns true if the section was
  // handled.
  bool
  add_merge_input_section(Relobj* object, unsigned int shndx, uint64_t flags,
			  uint64_t entsize, uint64_t addralign);

  // Add an output SHF_MERGE section POSD to this output section.
  // IS_STRING indicates whether it is a SHF_STRINGS section, and
  // ENTSIZE is the entity size.  This returns the entry added to
  // input_sections_.
  void
  add_output_merge_section(Output_section_data* posd, bool is_string,
			   uint64_t entsize);

  // Sort the attached input sections.
  void
  sort_attached_input_sections();

  // Most of these fields are only valid after layout.

  // The name of the section.  This will point into a Stringpool.
  const char* name_;
  // The section address is in the parent class.
  // The section alignment.
  uint64_t addralign_;
  // The section entry size.
  uint64_t entsize_;
  // The load address.  This is only used when using a linker script
  // with a SECTIONS clause.  The has_load_address_ field indicates
  // whether this field is valid.
  uint64_t load_address_;
  // The file offset is in the parent class.
  // Set the section link field to the index of this section.
  const Output_data* link_section_;
  // If link_section_ is NULL, this is the link field.
  unsigned int link_;
  // Set the section info field to the index of this section.
  const Output_section* info_section_;
  // If info_section_ is NULL, set the info field to the symbol table
  // index of this symbol.
  const Symbol* info_symndx_;
  // If info_section_ and info_symndx_ are NULL, this is the section
  // info field.
  unsigned int info_;
  // The section type.
  const elfcpp::Elf_Word type_;
  // The section flags.
  elfcpp::Elf_Xword flags_;
  // The section index.
  unsigned int out_shndx_;
  // If there is a STT_SECTION for this output section in the normal
  // symbol table, this is the symbol index.  This starts out as zero.
  // It is initialized in Layout::finalize() to be the index, or -1U
  // if there isn't one.
  unsigned int symtab_index_;
  // If there is a STT_SECTION for this output section in the dynamic
  // symbol table, this is the symbol index.  This starts out as zero.
  // It is initialized in Layout::finalize() to be the index, or -1U
  // if there isn't one.
  unsigned int dynsym_index_;
  // The input sections.  This will be empty in cases where we don't
  // need to keep track of them.
  Input_section_list input_sections_;
  // The offset of the first entry in input_sections_.
  off_t first_input_offset_;
  // The fill data.  This is separate from input_sections_ because we
  // often will need fill sections without needing to keep track of
  // input sections.
  Fill_list fills_;
  // If the section requires postprocessing, this buffer holds the
  // section contents during relocation.
  unsigned char* postprocessing_buffer_;
  // Whether this output section needs a STT_SECTION symbol in the
  // normal symbol table.  This will be true if there is a relocation
  // which needs it.
  bool needs_symtab_index_ : 1;
  // Whether this output section needs a STT_SECTION symbol in the
  // dynamic symbol table.  This will be true if there is a dynamic
  // relocation which needs it.
  bool needs_dynsym_index_ : 1;
  // Whether the link field of this output section should point to the
  // normal symbol table.
  bool should_link_to_symtab_ : 1;
  // Whether the link field of this output section should point to the
  // dynamic symbol table.
  bool should_link_to_dynsym_ : 1;
  // Whether this section should be written after all the input
  // sections are complete.
  bool after_input_sections_ : 1;
  // Whether this section requires post processing after all
  // relocations have been applied.
  bool requires_postprocessing_ : 1;
  // Whether an input section was mapped to this output section
  // because of a SECTIONS clause in a linker script.
  bool found_in_sections_clause_ : 1;
  // Whether this section has an explicitly specified load address.
  bool has_load_address_ : 1;
  // True if the info_section_ field means the section index of the
  // section, false if it means the symbol index of the corresponding
  // section symbol.
  bool info_uses_section_index_ : 1;
  // True if the input sections attached to this output section may
  // need sorting.
  bool may_sort_attached_input_sections_ : 1;
  // True if the input sections attached to this output section must
  // be sorted.
  bool must_sort_attached_input_sections_ : 1;
  // True if the input sections attached to this output section have
  // already been sorted.
  bool attached_input_sections_are_sorted_ : 1;
  // For SHT_TLS sections, the offset of this section relative to the base
  // of the TLS segment.
  uint64_t tls_offset_;
};

// An output segment.  PT_LOAD segments are built from collections of
// output sections.  Other segments typically point within PT_LOAD
// segments, and are built directly as needed.

class Output_segment
{
 public:
  // Create an output segment, specifying the type and flags.
  Output_segment(elfcpp::Elf_Word, elfcpp::Elf_Word);

  // Return the virtual address.
  uint64_t
  vaddr() const
  { return this->vaddr_; }

  // Return the physical address.
  uint64_t
  paddr() const
  { return this->paddr_; }

  // Return the segment type.
  elfcpp::Elf_Word
  type() const
  { return this->type_; }

  // Return the segment flags.
  elfcpp::Elf_Word
  flags() const
  { return this->flags_; }

  // Return the memory size.
  uint64_t
  memsz() const
  { return this->memsz_; }

  // Return the file size.
  off_t
  filesz() const
  { return this->filesz_; }

  // Return the file offset.
  off_t
  offset() const
  { return this->offset_; }

  // Return the maximum alignment of the Output_data.
  uint64_t
  maximum_alignment();

  // Add an Output_section to this segment.
  void
  add_output_section(Output_section* os, elfcpp::Elf_Word seg_flags)
  { this->add_output_section(os, seg_flags, false); }

  // Add an Output_section to the start of this segment.
  void
  add_initial_output_section(Output_section* os, elfcpp::Elf_Word seg_flags)
  { this->add_output_section(os, seg_flags, true); }

  // Remove an Output_section from this segment.  It is an error if it
  // is not present.
  void
  remove_output_section(Output_section* os);

  // Add an Output_data (which is not an Output_section) to the start
  // of this segment.
  void
  add_initial_output_data(Output_data*);

  // Return true if this segment has any sections which hold actual
  // data, rather than being a BSS section.
  bool
  has_any_data_sections() const
  { return !this->output_data_.empty(); }

  // Return the number of dynamic relocations applied to this segment.
  unsigned int
  dynamic_reloc_count() const;

  // Return the address of the first section.
  uint64_t
  first_section_load_address() const;

  // Return whether the addresses have been set already.
  bool
  are_addresses_set() const
  { return this->are_addresses_set_; }

  // Set the addresses.
  void
  set_addresses(uint64_t vaddr, uint64_t paddr)
  {
    this->vaddr_ = vaddr;
    this->paddr_ = paddr;
    this->are_addresses_set_ = true;
  }

  // Set the segment flags.  This is only used if we have a PHDRS
  // clause which explicitly specifies the flags.
  void
  set_flags(elfcpp::Elf_Word flags)
  { this->flags_ = flags; }

  // Set the address of the segment to ADDR and the offset to *POFF
  // and set the addresses and offsets of all contained output
  // sections accordingly.  Set the section indexes of all contained
  // output sections starting with *PSHNDX.  If RESET is true, first
  // reset the addresses of the contained sections.  Return the
  // address of the immediately following segment.  Update *POFF and
  // *PSHNDX.  This should only be called for a PT_LOAD segment.
  uint64_t
  set_section_addresses(const Layout*, bool reset, uint64_t addr, off_t* poff,
			unsigned int* pshndx);

  // Set the minimum alignment of this segment.  This may be adjusted
  // upward based on the section alignments.
  void
  set_minimum_p_align(uint64_t align)
  { this->min_p_align_ = align; }

  // Set the offset of this segment based on the section.  This should
  // only be called for a non-PT_LOAD segment.
  void
  set_offset();

  // Set the TLS offsets of the sections contained in the PT_TLS segment.
  void
  set_tls_offsets();

  // Return the number of output sections.
  unsigned int
  output_section_count() const;

  // Return the section attached to the list segment with the lowest
  // load address.  This is used when handling a PHDRS clause in a
  // linker script.
  Output_section*
  section_with_lowest_load_address() const;

  // Write the segment header into *OPHDR.
  template<int size, bool big_endian>
  void
  write_header(elfcpp::Phdr_write<size, big_endian>*);

  // Write the section headers of associated sections into V.
  template<int size, bool big_endian>
  unsigned char*
  write_section_headers(const Layout*, const Stringpool*, unsigned char* v,
			unsigned int* pshndx) const;

 private:
  Output_segment(const Output_segment&);
  Output_segment& operator=(const Output_segment&);

  typedef std::list<Output_data*> Output_data_list;

  // Add an Output_section to this segment, specifying front or back.
  void
  add_output_section(Output_section*, elfcpp::Elf_Word seg_flags,
		     bool front);

  // Find the maximum alignment in an Output_data_list.
  static uint64_t
  maximum_alignment_list(const Output_data_list*);

  // Set the section addresses in an Output_data_list.
  uint64_t
  set_section_list_addresses(const Layout*, bool reset, Output_data_list*,
                             uint64_t addr, off_t* poff, unsigned int* pshndx,
                             bool* in_tls);

  // Return the number of Output_sections in an Output_data_list.
  unsigned int
  output_section_count_list(const Output_data_list*) const;

  // Return the number of dynamic relocs in an Output_data_list.
  unsigned int
  dynamic_reloc_count_list(const Output_data_list*) const;

  // Find the section with the lowest load address in an
  // Output_data_list.
  void
  lowest_load_address_in_list(const Output_data_list* pdl,
			      Output_section** found,
			      uint64_t* found_lma) const;

  // Write the section headers in the list into V.
  template<int size, bool big_endian>
  unsigned char*
  write_section_headers_list(const Layout*, const Stringpool*,
			     const Output_data_list*, unsigned char* v,
			     unsigned int* pshdx) const;

  // The list of output data with contents attached to this segment.
  Output_data_list output_data_;
  // The list of output data without contents attached to this segment.
  Output_data_list output_bss_;
  // The segment virtual address.
  uint64_t vaddr_;
  // The segment physical address.
  uint64_t paddr_;
  // The size of the segment in memory.
  uint64_t memsz_;
  // The maximum section alignment.  The is_max_align_known_ field
  // indicates whether this has been finalized.
  uint64_t max_align_;
  // The required minimum value for the p_align field.  This is used
  // for PT_LOAD segments.  Note that this does not mean that
  // addresses should be aligned to this value; it means the p_paddr
  // and p_vaddr fields must be congruent modulo this value.  For
  // non-PT_LOAD segments, the dynamic linker works more efficiently
  // if the p_align field has the more conventional value, although it
  // can align as needed.
  uint64_t min_p_align_;
  // The offset of the segment data within the file.
  off_t offset_;
  // The size of the segment data in the file.
  off_t filesz_;
  // The segment type;
  elfcpp::Elf_Word type_;
  // The segment flags.
  elfcpp::Elf_Word flags_;
  // Whether we have finalized max_align_.
  bool is_max_align_known_ : 1;
  // Whether vaddr and paddr were set by a linker script.
  bool are_addresses_set_ : 1;
};

// This class represents the output file.

class Output_file
{
 public:
  Output_file(const char* name);

  // Indicate that this is a temporary file which should not be
  // output.
  void
  set_is_temporary()
  { this->is_temporary_ = true; }

  // Open the output file.  FILE_SIZE is the final size of the file.
  void
  open(off_t file_size);

  // Resize the output file.
  void
  resize(off_t file_size);

  // Close the output file (flushing all buffered data) and make sure
  // there are no errors.
  void
  close();

  // We currently always use mmap which makes the view handling quite
  // simple.  In the future we may support other approaches.

  // Write data to the output file.
  void
  write(off_t offset, const void* data, size_t len)
  { memcpy(this->base_ + offset, data, len); }

  // Get a buffer to use to write to the file, given the offset into
  // the file and the size.
  unsigned char*
  get_output_view(off_t start, size_t size)
  {
    gold_assert(start >= 0
                && start + static_cast<off_t>(size) <= this->file_size_);
    return this->base_ + start;
  }

  // VIEW must have been returned by get_output_view.  Write the
  // buffer to the file, passing in the offset and the size.
  void
  write_output_view(off_t, size_t, unsigned char*)
  { }

  // Get a read/write buffer.  This is used when we want to write part
  // of the file, read it in, and write it again.
  unsigned char*
  get_input_output_view(off_t start, size_t size)
  { return this->get_output_view(start, size); }

  // Write a read/write buffer back to the file.
  void
  write_input_output_view(off_t, size_t, unsigned char*)
  { }

  // Get a read buffer.  This is used when we just want to read part
  // of the file back it in.
  const unsigned char*
  get_input_view(off_t start, size_t size)
  { return this->get_output_view(start, size); }

  // Release a read bfufer.
  void
  free_input_view(off_t, size_t, const unsigned char*)
  { }

 private:
  // Map the file into memory and return a pointer to the map.
  void
  map();

  // Unmap the file from memory (and flush to disk buffers).
  void
  unmap();

  // File name.
  const char* name_;
  // File descriptor.
  int o_;
  // File size.
  off_t file_size_;
  // Base of file mapped into memory.
  unsigned char* base_;
  // True iff base_ points to a memory buffer rather than an output file.
  bool map_is_anonymous_;
  // True if this is a temporary file which should not be output.
  bool is_temporary_;
};

} // End namespace gold.

#endif // !defined(GOLD_OUTPUT_H)
